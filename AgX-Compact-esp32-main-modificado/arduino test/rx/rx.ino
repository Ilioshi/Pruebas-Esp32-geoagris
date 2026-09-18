#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

// ---------------- PINES ----------------
static constexpr int PIN_CS      = 5;
static constexpr int PIN_SCK     = 18;
static constexpr int PIN_MISO    = 19;
static constexpr int PIN_MOSI    = 23;
static constexpr int PIN_CAN_INT = 21;

// ---------------- J1939 ----------------
static constexpr uint32_t MAX_HOURS = 100000;
static constexpr uint8_t  SA_TX     = 0x80;
static constexpr uint32_t PGN_PROP_B = 0x00FF00;

#ifndef CAN_EFF_FLAG
  #define CAN_EFF_FLAG 0x80000000U
#endif
#ifndef CAN_EFF_MASK
  #define CAN_EFF_MASK 0x1FFFFFFFU
#endif

// ---------------- GLOBAL ----------------
static MCP2515 mcp2515(PIN_CS);
static uint32_t lastRecoverMs = 0;
TaskHandle_t canReadTaskHandle = NULL;

// ---------------- DEBUG ----------------
void printForDebugCan(const char* msg) {
  Serial.printf("[CAN] %s\n", msg);
}

// ---------------- J1939 HELPERS ----------------
static inline uint8_t j1939_pf(uint32_t id) { return (id >> 16) & 0xFF; }
static inline uint8_t j1939_ps(uint32_t id) { return (id >> 8)  & 0xFF; }
static inline uint8_t j1939_sa(uint32_t id) { return (id >> 0)  & 0xFF; }
static inline uint8_t j1939_dp(uint32_t id) { return (id >> 24) & 0x01; }

static inline uint32_t j1939_pgn(uint32_t id) {
  uint32_t pf = j1939_pf(id);
  return (j1939_dp(id) << 16) | (pf << 8) | ((pf < 240) ? 0 : j1939_ps(id));
}

static inline uint32_t get_u32_le(const uint8_t *p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

// ---------------- ISR ----------------
void IRAM_ATTR canIntISR() {
  BaseType_t hpTaskWoken = pdFALSE;
  xTaskNotifyFromISR(canReadTaskHandle, 0x01, eSetBits, &hpTaskWoken);
  if (hpTaskWoken) portYIELD_FROM_ISR();
}

// ---------------- TAREA CAN ----------------
void canReadTask(void *pvParameters) {

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  mcp2515.reset();
  mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);

  // aceptar todo
  mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
  mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
  for (int i = 0; i < 6; i++)
    mcp2515.setFilter((MCP2515::RXF)i, true, 0x00000000);

  mcp2515.setNormalMode();

  Serial.println("CAN RX listo (modo interrupción)");

  bool haveLast = false;
  uint32_t lastHours = 0;
  uint32_t good = 0, bad = 0, counter = 0;
  uint32_t t0 = millis();

  while (true) {

    // ⏳ Dormir hasta INT
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    struct can_frame frame;

    // 📥 Leer TODO lo pendiente
    while (mcp2515.readMessage(&frame) == MCP2515::ERROR_OK) {

      counter++;

      if ((frame.can_id & CAN_EFF_FLAG) == 0) continue;

      uint32_t id29 = frame.can_id & CAN_EFF_MASK;
      if (j1939_pgn(id29) != PGN_PROP_B) continue;
      if (j1939_sa(id29)  != SA_TX)      continue;
      if (frame.can_dlc   < 4)           continue;

      uint32_t hours = get_u32_le(frame.data);

      if (!haveLast) {
        haveLast = true;
        lastHours = hours;
        Serial.printf("INIT hours=%lu\n", hours);
        continue;
      }

      uint32_t expected = (lastHours == MAX_HOURS) ? 0 : lastHours + 1;

      if (hours != expected) {
        bad++;
        Serial.printf("ERROR exp=%lu rx=%lu last=%lu\n",
                      expected, hours, lastHours);
      } else {
        good++;
      }

      lastHours = hours;
    }

    // 📊 Estadísticas cada 10s
    if (millis() - t0 >= 10000) {
      Serial.printf("OK=%lu ERR=%lu TOTAL=%lu (%.2f%%)\n",
                    good, bad, counter,
                    counter ? (100.0 * good / counter) : 0);
      good = bad = counter = 0;
      t0 = millis();
    }

    // ⚠️ Manejo de errores MCP2515
    uint8_t eflg = mcp2515.getErrorFlags();

    if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
      mcp2515.clearRXnOVR();
      printForDebugCan("RX overflow");
    }

    if (eflg & (MCP2515::EFLG_RXEP | MCP2515::EFLG_TXEP | MCP2515::EFLG_TXBO)) {
      uint32_t now = millis();
      if (now - lastRecoverMs > 1000) {
        printForDebugCan("Recover MCP2515");
        mcp2515.reset();
        mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
        mcp2515.setNormalMode();
        lastRecoverMs = now;
      }
    }

    // 🔄 Limpiar interrupciones con verificación
    // Según datasheet p29, el pin INT se mantiene LOW mientras CANINTF != 0
    for (int retry = 0; retry < 5; retry++) {
      mcp2515.clearInterrupts();
      
      // Verificar que CANINTF se haya limpiado
      uint8_t canintf = mcp2515.getInterrupts();
      if (canintf == 0) {
        break; // Limpiado exitosamente
      }
      
      if (retry == 4) {
        Serial.printf("[WARN] CANINTF no se limpio: 0x%02X\n", canintf);
        // Forzar escritura directa
        mcp2515.clearInterrupts();
      }
    }
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // BLE (activo, no bloquea)
  BLEDevice::init("ESP32-CAN-BLE");
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->start();

  pinMode(PIN_CAN_INT, INPUT_PULLUP);

  xTaskCreatePinnedToCore(
    canReadTask,
    "CAN_RX",
    4096,
    NULL,
    24,
    &canReadTaskHandle,
    0
  );

  attachInterrupt(
    digitalPinToInterrupt(PIN_CAN_INT),
    canIntISR,
    FALLING
  );

  Serial.println("Setup completo");
}

void loop() {
  // nada
}
