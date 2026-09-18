#ifdef ENABLE_CAN_SNIFFER

#include "CAN/CAN_sniffer.h"
#include <Arduino.h>
#include <SPI.h>
#include "mcp2515.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// --------- Pines ----------
static constexpr int PIN_CS   = 5;
static constexpr int PIN_SCK  = 18;
static constexpr int PIN_MISO = 19;
static constexpr int PIN_MOSI = 23;
static constexpr int PIN_INT  = 21;  // Pin de interrupción del MCP2515

// --------- J1939 Configuration ----------
static constexpr uint8_t  SA_TX     = 0x80;      // debe coincidir con TX

// PGNs del transmisor
static constexpr uint32_t PGN_RPM_TORQUE        = 0xF004;
static constexpr uint32_t PGN_FUEL_CONSUMPTION  = 0xFEF2;
static constexpr uint32_t PGN_ENGINE_TEMP       = 0xFEEE;
static constexpr uint32_t PGN_ENGINE_HOURS      = 0xFEE5;

#ifndef CAN_EFF_FLAG
  #define CAN_EFF_FLAG 0x80000000U
#endif
#ifndef CAN_EFF_MASK
  #define CAN_EFF_MASK 0x1FFFFFFFU
#endif

static MCP2515 mcp2515(PIN_CS);
static uint32_t lastRecoverMs = 0;

// 0 = solo polling, 1 = interrupción + polling de respaldo
#ifndef CAN_USE_INTERRUPT
#define CAN_USE_INTERRUPT 1
#endif

// 0 = sin prints por error individuales, 1 = prints detallados
#ifndef CAN_DEBUG_ERRORS
#define CAN_DEBUG_ERRORS 0
#endif

#if CAN_USE_INTERRUPT
static SemaphoreHandle_t g_canRxSemaphore = nullptr;

static void IRAM_ATTR canISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (g_canRxSemaphore != nullptr) {
    xSemaphoreGiveFromISR(g_canRxSemaphore, &xHigherPriorityTaskWoken);
  }
  if (xHigherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}
#endif

// Helper para debug
static void printForDebugCan(const char* msg) {
  (void)msg;
#if CAN_DEBUG_ERRORS
  Serial.printf("[CAN SNIFFER] %s\n", msg);
#endif
}

// Funciones J1939 helpers
static inline uint8_t j1939_sa(uint32_t id29) { return (id29 >> 0)  & 0xFF; }
static inline uint8_t j1939_dp(uint32_t id29) { return (id29 >> 24) & 0x01; }
static inline uint8_t j1939_pf(uint32_t id29) { return (id29 >> 16) & 0xFF; }
static inline uint8_t j1939_ps(uint32_t id29) { return (id29 >> 8)  & 0xFF; }

static inline uint32_t j1939_pgn(uint32_t id29) {
  uint32_t dp = j1939_dp(id29);
  uint32_t pf = j1939_pf(id29);
  uint32_t ps = j1939_ps(id29);
  return (dp << 16) | (pf << 8) | ((pf < 240) ? 0 : ps);
}

static inline uint16_t get_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t get_u32_le(const uint8_t *p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

// ========== ESTRUCTURAS Y VARIABLES DE TRACKING ==========

struct PacketStats {
  bool initialized;
  uint32_t lastValue;
  uint32_t good;
  uint32_t bad;
  uint32_t minValue;
  uint32_t maxValue;
  const char* name;
};

static PacketStats statsRPM = {false, 0, 0, 0, 800, 3000, "RPM"};
static PacketStats statsTorque = {false, 0, 0, 0, 0, 100, "Torque"};
static PacketStats statsFuel = {false, 0, 0, 0, 5, 50, "Fuel"};
static PacketStats statsTemp = {false, 0, 0, 0, 20, 120, "Temp"};
static PacketStats statsHours = {false, 0, 0, 0, 0, UINT32_MAX, "Hours"};

// Contador total de mensajes
static uint32_t totalMessagesReceived = 0;
static uint32_t totalMessagesProcessed = 0;

// ========== FUNCIONES DE PROCESAMIENTO ==========

static void checkSequence(PacketStats* stats, uint32_t value) {
  if (!stats->initialized) {
    stats->initialized = true;
    stats->lastValue = value;
    (void)value;
#if CAN_DEBUG_ERRORS
    Serial.printf("[%s] INIT value=%lu\n", stats->name, (unsigned long)value);
#endif
    return;
  }

  // Calcular valor esperado con wrapping
  uint32_t expected;
  if (stats->maxValue == UINT32_MAX) {
    // Sin wrapping (Hours)
    expected = stats->lastValue + 1;
  } else {
    // Con wrapping
    expected = (stats->lastValue == stats->maxValue) ? stats->minValue : (stats->lastValue + 1);
  }

  if (value != expected) {
    stats->bad++;
#if CAN_DEBUG_ERRORS
    Serial.printf("[%s] ERROR: esperado=%lu, recibido=%lu (last=%lu)\n",
                  stats->name,
                  (unsigned long)expected,
                  (unsigned long)value,
                  (unsigned long)stats->lastValue);
#endif
  } else {
    stats->good++;
  }
  
  stats->lastValue = value;
}

static void processRPMTorque(const uint8_t* data, uint8_t len) {
  if (len < 8) return;

  // RPM está en bytes 6-7 (resolución 0.125 rpm/bit)
  uint16_t rpmScaled = get_u16_le(&data[6]);
  if (rpmScaled != 0xFFFF) {
    uint32_t rpm = rpmScaled / 8;
    checkSequence(&statsRPM, rpm);
  }

  // Torque está en byte 4 (offset 125)
  if (data[4] != 0xFF) {
    uint32_t torque = (uint32_t)data[4] - 125;
    checkSequence(&statsTorque, torque);
  }
}

static void processFuelConsumption(const uint8_t* data, uint8_t len) {
  if (len < 2) return;

  // Fuel está en bytes 0-1 (resolución 0.05 L/h)
  uint16_t fuelScaled = get_u16_le(&data[0]);
  if (fuelScaled != 0xFFFF) {
    uint32_t fuel = fuelScaled / 20;
    checkSequence(&statsFuel, fuel);
  }
}

static void processEngineTemp(const uint8_t* data, uint8_t len) {
  if (len < 1) return;

  // Temp está en byte 0 (offset -40°C)
  if (data[0] != 0xFF) {
    uint32_t temp = (uint32_t)data[0] - 40;
    checkSequence(&statsTemp, temp);
  }
}

static void processEngineHours(const uint8_t* data, uint8_t len) {
  if (len < 4) return;

  // Hours está en bytes 0-3 (sin escala para el test)
  uint32_t hours = get_u32_le(&data[0]);
  if (hours != 0xFFFFFFFF) {
    checkSequence(&statsHours, hours);
  }
}

static void printStats(PacketStats* stats, uint32_t elapsedMs) {
  uint32_t total = stats->good + stats->bad;
  if (total == 0) return;

  float rate = ((float)stats->good / elapsedMs) * 1000.0f;
  float successRate = ((float)stats->good / total) * 100.0f;

  Serial.printf("[%s] Good/sec: %.2f | Aciertos: %lu | Errores: %lu | Tasa: %.2f%%\n",
                stats->name,
                rate,
                (unsigned long)stats->good,
                (unsigned long)stats->bad,
                successRate);
}

static void resetStats(PacketStats* stats) {
  stats->good = 0;
  stats->bad = 0;
}

// ========== TAREA PRINCIPAL ==========

void canSnifferTask(void *pvParameters) {
  (void)pvParameters;
  uint32_t lastReportTime = millis();

  // Inicialización del CAN dentro de la tarea
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  mcp2515.reset();
  mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);

  // Aceptar todo
  mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
  mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF0, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF1, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF2, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF3, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF4, true, 0x00000000);
  mcp2515.setFilter(MCP2515::RXF5, true, 0x00000000);

  mcp2515.setNormalMode();

#if CAN_USE_INTERRUPT
  if (g_canRxSemaphore == nullptr) {
    g_canRxSemaphore = xSemaphoreCreateBinary();
    // arrancar "tomado" para que el primer take bloquee hasta primera INT o timeout
    xSemaphoreTake(g_canRxSemaphore, 0);
  }
  pinMode(PIN_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_INT), canISR, FALLING);
#endif

  Serial.println("CAN SNIFFER iniciado (INT + polling)");

  const TickType_t waitTicks = pdMS_TO_TICKS(5);  // timeout de polling de respaldo

  while (true) {
#if CAN_USE_INTERRUPT
    // Esperar interrupción o timeout para hacer polling de respaldo
    (void)xSemaphoreTake(g_canRxSemaphore, waitTicks);
#else
    vTaskDelay(waitTicks);
#endif

    // Procesar TODOS los mensajes disponibles
    struct can_frame frame;
    while (mcp2515.readMessage(&frame) == MCP2515::ERROR_OK) {
      totalMessagesReceived++;

      // Verificar que sea extended frame
      if ((frame.can_id & CAN_EFF_FLAG) == 0) continue;

      uint32_t id29 = frame.can_id & CAN_EFF_MASK;
      uint32_t pgn  = j1939_pgn(id29);
      uint8_t  sa   = j1939_sa(id29);

      // Verificar que venga del transmisor correcto
      if (sa != SA_TX) continue;

      totalMessagesProcessed++;

      // Procesar según PGN
      switch (pgn) {
        case PGN_RPM_TORQUE:
          processRPMTorque(frame.data, frame.can_dlc);
          break;
        case PGN_FUEL_CONSUMPTION:
          processFuelConsumption(frame.data, frame.can_dlc);
          break;
        case PGN_ENGINE_TEMP:
          processEngineTemp(frame.data, frame.can_dlc);
          break;
        case PGN_ENGINE_HOURS:
          processEngineHours(frame.data, frame.can_dlc);
          break;
        default:
          break;
      }
    }

    // Reporte cada 10 segundos
    uint32_t nowMs = millis();
    if (nowMs - lastReportTime >= 10000) {
      uint32_t elapsed = nowMs - lastReportTime;
      float totalRate  = (elapsed > 0)
                           ? ((float)totalMessagesProcessed / elapsed) * 1000.0f
                           : 0.0f;

      Serial.printf("Intervalo: %lu ms\n", (unsigned long)elapsed);
      Serial.printf("TOTAL RX: %lu | Procesados: %lu | Rate: %.2f msg/s\n",
                    (unsigned long)totalMessagesReceived,
                    (unsigned long)totalMessagesProcessed,
                    totalRate);

      printStats(&statsRPM, elapsed);
      printStats(&statsTorque, elapsed);
      printStats(&statsFuel, elapsed);
      printStats(&statsTemp, elapsed);
      printStats(&statsHours, elapsed);

      // Reset contadores
      resetStats(&statsRPM);
      resetStats(&statsTorque);
      resetStats(&statsFuel);
      resetStats(&statsTemp);
      resetStats(&statsHours);
      totalMessagesReceived  = 0;
      totalMessagesProcessed = 0;

      lastReportTime = nowMs;
    }

    // ===== MANEJO DE ERRORES MCP2515 =====
    uint8_t eflg = mcp2515.getErrorFlags();

    if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
      mcp2515.clearRXnOVR();
      printForDebugCan("RX buffer overflow cleared");
    }

    if (eflg & (MCP2515::EFLG_RXEP | MCP2515::EFLG_TXEP | MCP2515::EFLG_TXBO)) {
      uint32_t nowErr = millis();
      if (nowErr - lastRecoverMs > 1000) {
        printForDebugCan("Critical error - recovering MCP2515");
        mcp2515.clearERRIF();
        mcp2515.clearMERR();
        mcp2515.reset();
        mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
        mcp2515.setNormalMode();
        lastRecoverMs = nowErr;
      }
    }
    // ===== FIN MANEJO DE ERRORES =====
  }
}

#endif  // ENABLE_CAN_SNIFFER