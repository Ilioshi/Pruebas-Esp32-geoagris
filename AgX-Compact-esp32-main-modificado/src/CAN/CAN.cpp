#include "CAN/CAN.h"
#include <Arduino.h>
#include <mcp2515.h>
#include <string>
#include <algorithm>
#include "CAN/CAN_protocols/isobus_protocol.h"
#include "CAN/CAN_protocols/j1939_protocol.h"
#include "CAN/CAN_protocols/VG55R.h"
#include "DutCanDiagnostic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**********************************************************************
*                             Defines                                 *
***********************************************************************/
#define PIN_CS    5
#define PIN_SCK   18
#define PIN_MISO  19
#define PIN_MOSI  23
#define PIN_INT   21


// 0 = solo polling, 1 = interrupción + polling de respaldo
#ifndef CAN_USE_INTERRUPT
  #define CAN_USE_INTERRUPT 1
#endif

// 0 = sin prints por error individuales, 1 = prints detallados
#ifndef CAN_DEBUG_ERRORS
  #define CAN_DEBUG_ERRORS 0
#endif

#define J1939_ENABLE 1
#define ISOBUS_ENABLE 1
#define VG55R_ENABLE 1

/**********************************************************************
*                         Global Variables                            *
***********************************************************************/

// Variables globales del CAN
static bool G_vg55rEnabled = false; // TODO: limpiar implementacion de VG55R

static Vg55rData vg55rState;
static uint32_t vg55rSeq = 0;


MCP2515 G_can_mcp2515(PIN_CS);

// Mutex para proteger VG55R
static SemaphoreHandle_t vg55rMutex = NULL;

#if CAN_USE_INTERRUPT
  static SemaphoreHandle_t g_canRxSemaphore = NULL;

/**********************************************************************
*                           ISRs Definitions                          *
***********************************************************************/
  
  static void IRAM_ATTR canISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (g_canRxSemaphore != NULL) {
      xSemaphoreGiveFromISR(g_canRxSemaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken == pdTRUE) {
      portYIELD_FROM_ISR();
    }
  }
#endif

// Driver VG55R (usa el mismo MCP2515 que el resto del modulo CAN)
static VG55R_Driver vg55rDriver(&G_can_mcp2515, 0x05);

/**********************************************************************
*                       Local Functions Prototypes                    *
***********************************************************************/

static void printForDebugCan(const char* msg);
static void canInitialize();
static bool configureMcp2515();
static bool canReady = false;
bool canGetVg55rData(Vg55rData& out, uint32_t& seq);  // TODO: es una verga esto, hacer archivo para todo

/**********************************************************************
*                             Main Task                               *
***********************************************************************/

void canReadTask(void *pvParameters) {
  canInitialize();
  uint32_t lastRecoverMs = 0;
  uint32_t lastVg55rStamp = 0;
  uint32_t lastHealthMs = 0;
  uint32_t busOffSince = 0;
    
  const TickType_t waitTicks = pdMS_TO_TICKS(5);  // timeout de polling de respaldo
    
  while(true) {
    dutCanLoop(); // Independent heartbeat: no received frames required.
    #if CAN_USE_INTERRUPT
    // Esperar interrupción o timeout para hacer polling de respaldo
    (void)xSemaphoreTake(g_canRxSemaphore, waitTicks);
    #else
    vTaskDelay(waitTicks);
    #endif
  
    if (!canReady) {
      if (millis() - lastRecoverMs >= 2000) {
        lastRecoverMs = millis();
        canReady = configureMcp2515();
      }
      continue; // Do not decode garbage if SPI/configuration failed.
    }

    struct can_frame canMsg;
    MCP2515::ERROR readResult = MCP2515::ERROR_NOMSG;
    uint8_t drained = 0;
    while (drained++ < 64 && (readResult = G_can_mcp2515.readMessage(&canMsg)) == MCP2515::ERROR_OK) {
      dutCanReceived(canMsg.can_id, canMsg.can_dlc);
      // Mensaje válido: procesar y marcar actividad
      #if J1939_ENABLE
        printForDebugCan("J1939 message");
        j1939ProcessMessage(canMsg.can_id, canMsg.data, canMsg.can_dlc);
      #endif

      #if ISOBUS_ENABLE
        printForDebugCan("ISOBUS message");
        isobusProcessMessage(canMsg.can_id, canMsg.data, canMsg.can_dlc);
      #endif
      
      // Procesar frames para VG55R (sensor de inclinacion)
      #if VG55R_ENABLE
        if (xSemaphoreTake(vg55rMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
          printForDebugCan("VG55R message");
          vg55rDriver.processFrame(canMsg);
          xSemaphoreGive(vg55rMutex);
        }
      #endif
    }

    #if VG55R_ENABLE
      if (vg55rMutex != NULL && (xSemaphoreTake(vg55rMutex, pdMS_TO_TICKS(10)) == pdTRUE)) {
        VG55R_Angles ang = vg55rDriver.getLastAngles();
        xSemaphoreGive(vg55rMutex);

        if (ang.timestamp != lastVg55rStamp) {
          lastVg55rStamp = ang.timestamp;
          vg55rState.valid = ang.valid;
          vg55rState.pitch = ang.pitch;
          vg55rState.roll = ang.roll;
          vg55rState.yaw = ang.yaw;
          vg55rState.timestamp = ang.timestamp;
          vg55rSeq++;
        }
      }
    #endif

    // Limpieza defensiva de overflow RX
    uint8_t eflg = G_can_mcp2515.getErrorFlags();
    const bool overflow = eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR);
    const bool readError = readResult != MCP2515::ERROR_OK && readResult != MCP2515::ERROR_NOMSG;
    if (millis() - lastHealthMs >= 200 || overflow || readError) {
      lastHealthMs = millis();
      dutCanHealth(eflg, G_can_mcp2515.errorCountRX(), G_can_mcp2515.errorCountTX(), readError, overflow);
    }
    if (overflow) {
      G_can_mcp2515.clearRXnOVR();
    }

    // Error-passive still participates in CAN. Resetting on RXEP repeatedly
    // interrupted reception and erased the evidence. Only recover bus-off;
    // first allow time for the controller's automatic bus recovery.
    if (eflg & MCP2515::EFLG_TXBO) {
      uint32_t now = millis();
      if (busOffSince == 0) busOffSince = now;
      if (now - busOffSince >= 10000) {
        printForDebugCan("Recovering MCP2515 from error state");
        canReady = configureMcp2515();
        lastRecoverMs = now;
        busOffSince = 0;
      }
    } else busOffSince = 0;
  }
}


/**********************************************************************
*                       Local Functions Definitions                   *
***********************************************************************/

// Helper para debug
static void printForDebugCan(const char* msg) {
  (void)msg;
#if CAN_DEBUG_ERRORS
  Serial.printf("[DEBUG CAN] %s\n", msg);
#endif
}

// Inicialización del módulo CAN
static void canInitialize() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  vg55rMutex = xSemaphoreCreateMutex();
  isobusDataMutex = xSemaphoreCreateMutex();
  j1939DataMutex = xSemaphoreCreateMutex();
    
  // Inicialización del CAN dentro de la tarea
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  canReady = configureMcp2515();
  printForDebugCan(canReady ? "MCP2515 Initialized Successfully!" : "MCP2515 initialization FAILED");
    
  // Inicializar driver VG55R
  if (G_vg55rEnabled) {
      VG55R_Error err = vg55rDriver.begin();
      if (err != VG55R_OK) {
          printForDebugCan("VG55R driver begin() failed");
          G_vg55rEnabled = false;
      } else {
          vg55rDriver.startAutoDetect();
          printForDebugCan("VG55R driver initialized");
      }
  }
  // HACK: Acceder de 2 formas a la lectura CAN (INT + polling de respaldo)
  #if CAN_USE_INTERRUPT
    if (g_canRxSemaphore == NULL) {
      g_canRxSemaphore = xSemaphoreCreateBinary();
      // arrancar "tomado" para que el primer take bloquee hasta primera INT o timeout
      xSemaphoreTake(g_canRxSemaphore, 0);
    }
    pinMode(PIN_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_INT), canISR, FALLING);
  #endif

  printForDebugCan("CAN RX iniciado (INT + polling)");
}

static bool configureMcp2515() {
  // 255 means the stage was not attempted. Preserve the first actual error.
  const uint8_t reset = G_can_mcp2515.reset();
  uint8_t bitrate = 255, filters = 255, mode = 255;
  if (reset == MCP2515::ERROR_OK) bitrate = G_can_mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
  if (bitrate == MCP2515::ERROR_OK) {
    filters = G_can_mcp2515.setFilterMask(MCP2515::MASK0, true, 0);
    if (filters == MCP2515::ERROR_OK) filters = G_can_mcp2515.setFilterMask(MCP2515::MASK1, true, 0);
    const MCP2515::RXF filterIds[] = {MCP2515::RXF0, MCP2515::RXF1, MCP2515::RXF2,
                                    MCP2515::RXF3, MCP2515::RXF4, MCP2515::RXF5};
    for (auto filter : filterIds) {
      if (filters == MCP2515::ERROR_OK) filters = G_can_mcp2515.setFilter(filter, true, 0);
    }
    if (filters == MCP2515::ERROR_OK) mode = G_can_mcp2515.setNormalMode();
  }
  dutCanInitResult(reset, bitrate, filters, mode);
  return reset == 0 && bitrate == 0 && filters == 0 && mode == 0;
}

bool canGetVg55rData(Vg55rData& out, uint32_t& seq) {   // TODO: sacar esto de este archivo
  if (vg55rMutex == NULL) {
    return false;
  }
  if (xSemaphoreTake(vg55rMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
    return false;
  }
  out = vg55rState;
  seq = vg55rSeq;
  xSemaphoreGive(vg55rMutex);
  return seq != 0;
}
