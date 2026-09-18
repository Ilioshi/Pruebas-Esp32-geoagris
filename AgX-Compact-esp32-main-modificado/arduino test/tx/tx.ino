#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

// --------- Pines (VSPI típico ESP32) ----------
static constexpr int PIN_CS   = 5;
static constexpr int PIN_SCK  = 18;
static constexpr int PIN_MISO = 19;
static constexpr int PIN_MOSI = 23;

// --------- J1939 Configuration ----------
static constexpr uint8_t  SA_TX     = 0x80;    // source address del emisor
static constexpr uint8_t  PRIORITY  = 6;       // típico J1939

// PGNs estándar J1939 (los mismos que usas en tu receptor)
static constexpr uint32_t PGN_RPM_TORQUE        = 0xF004; // RPM y Torque
static constexpr uint32_t PGN_FUEL_CONSUMPTION  = 0xFEF2; // Consumo de Combustible
static constexpr uint32_t PGN_ENGINE_TEMP       = 0xFEEE; // Temperatura del Motor
static constexpr uint32_t PGN_ENGINE_HOURS      = 0xFEE5; // Horas del Motor

// Flags de CAN
#ifndef CAN_EFF_FLAG
  #define CAN_EFF_FLAG 0x80000000U
#endif
#ifndef CAN_EFF_MASK
  #define CAN_EFF_MASK 0x1FFFFFFFU
#endif

static MCP2515 mcp2515(PIN_CS);

// Variables de datos del motor
static uint16_t rpm = 800;                    // RPM inicial
static uint8_t torque = 0;                    // Torque %
static float fuelConsumption = 5.0f;          // L/h
static uint8_t engineTemperature = 20;        // °C
static uint32_t engineHours = 0;              // horas * 20 (resolución 0.05h)

static inline uint32_t j1939_build_id(uint32_t pgn, uint8_t sa, uint8_t priority) {
  // Construir el CAN ID 29-bit según J1939
  // PGN ya incluye DP, PF y PS en los bits correctos
  uint32_t id29 = ((uint32_t)(priority & 0x7) << 26)
                | ((pgn & 0x3FFFF) << 8)
                | (uint32_t)sa;
  return id29;
}

static inline void put_u16_le(uint8_t *dst, uint16_t v) {
  dst[0] = (uint8_t)(v & 0xFF);
  dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void put_u32_le(uint8_t *dst, uint32_t v) {
  dst[0] = (uint8_t)(v & 0xFF);
  dst[1] = (uint8_t)((v >> 8) & 0xFF);
  dst[2] = (uint8_t)((v >> 16) & 0xFF);
  dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

void sendJ1939Message(uint32_t pgn, uint8_t *data, uint8_t dlc) {
  struct can_frame frame;
  memset(&frame, 0, sizeof(frame));

  uint32_t id29 = j1939_build_id(pgn, SA_TX, PRIORITY);
  frame.can_id  = CAN_EFF_FLAG | (id29 & CAN_EFF_MASK);
  frame.can_dlc = dlc;
  memcpy(frame.data, data, dlc);

  MCP2515::ERROR err = mcp2515.sendMessage(&frame);
  if (err != MCP2515::ERROR_OK) {
    Serial.printf("TX ERROR=%d (PGN=0x%04X)\n", (int)err, (unsigned int)pgn);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  mcp2515.reset();
  mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  Serial.println("TX J1939 listo - PGNs: 0xF004, 0xFEF2, 0xFEEE, 0xFEE5");
}

void loop() {
  static uint32_t lastSendTime = 0;
  uint32_t now = millis();

  // Enviar secuencia completa cada 50ms (cada mensaje con 10ms de separación)
  if (now - lastSendTime >= 50) {
    lastSendTime = now;

    uint8_t data[8];

    // Mensaje 1: PGN 0xF004 - RPM y Torque
    // Bytes 0-1: Engine Torque Mode (0xFF = not available)
    // Bytes 2-3: Driver's Demand Engine Torque (0xFF = not available)
    // Bytes 4-5: Actual Engine Torque (0-125%, offset 125, resolution 1%)
    // Bytes 6-7: Engine Speed (RPM, resolution 0.125 rpm/bit)
    memset(data, 0xFF, 8);
    uint8_t torqueJ1939 = torque + 125; // offset 125
    data[4] = torqueJ1939;
    data[5] = 0xFF;
    uint16_t rpmScaled = (uint16_t)(rpm * 8.0f); // 0.125 rpm/bit
    put_u16_le(&data[6], rpmScaled);
    sendJ1939Message(PGN_RPM_TORQUE, data, 8);
    delay(10);

    // Mensaje 2: PGN 0xFEF2 - Consumo de Combustible
    // Bytes 0-1: Engine Fuel Rate (L/h, resolution 0.05 L/h)
    memset(data, 0xFF, 8);
    uint16_t fuelScaled = (uint16_t)(fuelConsumption * 20.0f); // 0.05 L/h/bit
    put_u16_le(data, fuelScaled);
    sendJ1939Message(PGN_FUEL_CONSUMPTION, data, 8);
    delay(10);

    // Mensaje 3: PGN 0xFEEE - Temperatura del Motor
    // Byte 0: Engine Coolant Temperature (offset -40°C, resolution 1°C)
    memset(data, 0xFF, 8);
    data[0] = (uint8_t)(engineTemperature + 40); // offset -40
    sendJ1939Message(PGN_ENGINE_TEMP, data, 8);
    delay(10);

    // Mensaje 4: PGN 0xFEE5 - Horas del Motor
    // Bytes 0-3: Engine Total Hours of Operation (resolution 0.05h, 32-bit)
    memset(data, 0xFF, 8);
    put_u32_le(data, engineHours);
    sendJ1939Message(PGN_ENGINE_HOURS, data, 8);

    // Incrementar valores para simulación (de 1 en 1 para debugging)
    rpm++;
    if (rpm > 3000) rpm = 800;
    
    torque++;
    if (torque > 100) torque = 0;
    
    fuelConsumption += 1.0f;
    if (fuelConsumption > 50.0f) fuelConsumption = 5.0f;
    
    engineTemperature++;
    if (engineTemperature > 120) engineTemperature = 20;
    
    engineHours++; // incrementa 0.05h cada ciclo
  }
}