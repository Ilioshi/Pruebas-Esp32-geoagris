#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>

// ============================================================
// GENERADOR CAN DE DIAGNOSTICO - GEOAGRIS
// ESP32 + MCP2515
//
// SPI ESP32:
//   SCK  = GPIO18
//   MISO = GPIO19
//   MOSI = GPIO23
//   CS   = GPIO5
//
// CAN:
//   250 kbps
//   MCP2515 con cristal de 8 MHz
//   IDs extendidos (29 bits)
//
// Este sketch envia exactamente las mismas 14 tramas del
// generador original, pero agrega diagnostico del MCP2515.
// ============================================================

#define CAN_CS_PIN   5
#define CAN_SCK_PIN  18
#define CAN_MISO_PIN 19
#define CAN_MOSI_PIN 23

MCP_CAN CAN0(CAN_CS_PIN);

// Registros MCP2515
static const uint8_t MCP_READ_CMD = 0x03;
static const uint8_t REG_CANSTAT  = 0x0E;
static const uint8_t REG_TEC      = 0x1C;
static const uint8_t REG_REC      = 0x1D;
static const uint8_t REG_CANINTF  = 0x2C;
static const uint8_t REG_EFLG     = 0x2D;

struct TestFrame {
  const char *name;
  uint32_t id;
  uint8_t data[8];
};

// MISMAS tramas del archivo original, en el mismo orden.
const TestFrame frames[] = {
  {"RPM",               0x0CF00400UL, {0x00,0x00,0x80,0xF0,0x29,0x00,0x00,0x00}},
  {"Consumo combustible",0x18FEF200UL, {0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
  {"Temperatura motor", 0x18FEEE00UL, {0x71,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
  {"Horas motor",       0x18FEE500UL, {0xAB,0xE0,0x00,0x00,0x00,0x00,0x00,0x00}},
  {"Coordenada",        0x0CFEF300UL, {0x6C,0xF6,0x84,0xEC,0xCA,0xC2,0xFB,0xDB}},
  {"Fecha/hora",        0x0CFEE600UL, {0x00,0x36,0x11,0x02,0x50,0x19,0x00,0x00}},
  {"Velocidad/direccion",0x0CFEE800UL,{0x32,0x00,0x14,0x00,0x00,0x00,0x00,0x00}},
  {"Setpoint volumen",  0x0CCB0000UL, {0x13,0x00,0x02,0x00,0x60,0xE3,0x16,0x00}},
  {"Volumen",           0x0CCB0000UL, {0x13,0x00,0x02,0x00,0x60,0xE3,0x16,0x00}},
  {"Estado trabajo",    0x0CCB0000UL, {0x00,0x00,0x00,0x8D,0x01,0x00,0x00,0x00}},
  {"Setpoint masa",     0x18CBF700UL, {0x13,0x00,0x06,0x00,0xC8,0xAF,0x00,0x00}},
  {"Masa",              0x18CB0000UL, {0x33,0x13,0x07,0x00,0xC8,0xAF,0x00,0x00}},
  {"Presion bomba",     0x18CB0000UL, {0x13,0x00,0xC2,0x00,0xE0,0x70,0x72,0x00}},
  {"Secciones",         0x18CB0000UL, {0x33,0x00,0xA1,0x00,0x55,0x55,0x01,0x00}}
};

const size_t FRAME_COUNT = sizeof(frames) / sizeof(frames[0]);

uint32_t totalCycles = 0;
uint32_t totalTxOK = 0;
uint32_t totalTxFail = 0;
uint32_t consecutiveFailedCycles = 0;

uint8_t mcpReadRegister(uint8_t address) {
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CAN_CS_PIN, LOW);
  SPI.transfer(MCP_READ_CMD);
  SPI.transfer(address);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(CAN_CS_PIN, HIGH);
  SPI.endTransaction();
  return value;
}

void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

void printData(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    printHexByte(data[i]);
    if (i + 1 < len) Serial.print(' ');
  }
}

void printEflgDecoded(uint8_t eflg) {
  if (eflg == 0) {
    Serial.print("ninguno");
    return;
  }
  if (eflg & 0x01) Serial.print("EWARN ");
  if (eflg & 0x02) Serial.print("RXWAR ");
  if (eflg & 0x04) Serial.print("TXWAR ");
  if (eflg & 0x08) Serial.print("RXEP ");
  if (eflg & 0x10) Serial.print("TXEP ");
  if (eflg & 0x20) Serial.print("TXBO ");
  if (eflg & 0x40) Serial.print("RX0OVR ");
  if (eflg & 0x80) Serial.print("RX1OVR ");
}

void printMcpStatus(const char *prefix) {
  uint8_t canstat = mcpReadRegister(REG_CANSTAT);
  uint8_t tec = mcpReadRegister(REG_TEC);
  uint8_t rec = mcpReadRegister(REG_REC);
  uint8_t canintf = mcpReadRegister(REG_CANINTF);
  uint8_t eflg = mcpReadRegister(REG_EFLG);

  Serial.print(prefix);
  Serial.print(" CANSTAT=0x"); printHexByte(canstat);
  Serial.print(" TEC="); Serial.print(tec);
  Serial.print(" REC="); Serial.print(rec);
  Serial.print(" CANINTF=0x"); printHexByte(canintf);
  Serial.print(" EFLG=0x"); printHexByte(eflg);
  Serial.print(" ["); printEflgDecoded(eflg); Serial.println("]");
}

bool initCAN() {
  Serial.println("Inicializando MCP2515...");
  for (int attempt = 1; attempt <= 5; attempt++) {
    byte result = CAN0.begin(MCP_ANY, CAN_250KBPS, MCP_8MHZ);
    if (result == CAN_OK) {
      CAN0.setMode(MCP_NORMAL);
      delay(50);
      Serial.println("MCP2515: OK - NORMAL, 250 kbps, 8 MHz");
      printMcpStatus("Estado inicial:");
      return true;
    }
    Serial.print("Intento "); Serial.print(attempt);
    Serial.print(" fallo. Codigo begin()="); Serial.println(result);
    delay(500);
  }
  Serial.println("FATAL: no se pudo inicializar el MCP2515.");
  return false;
}

bool sendTestFrame(const TestFrame &f, size_t index) {
  Serial.print("TX ["); Serial.print(index + 1); Serial.print('/'); Serial.print(FRAME_COUNT); Serial.print("] ");
  Serial.print(f.name);
  Serial.print("  ID=0x"); Serial.print(f.id, HEX);
  Serial.print(" DLC=8 DATA="); printData(f.data, 8);

  byte stat = CAN0.sendMsgBuf(f.id, 1, 8, const_cast<uint8_t*>(f.data));
  if (stat == CAN_OK) {
    Serial.println("  -> OK");
    totalTxOK++;
    return true;
  }

  Serial.print("  -> ERROR sendMsgBuf="); Serial.println(stat);
  totalTxFail++;
  printMcpStatus("  MCP2515:");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" GENERADOR CAN - DIAGNOSTICO COMPLETO");
  Serial.println("============================================================");
  Serial.println("ESP32 SPI: SCK=18 MISO=19 MOSI=23 CS=5");
  Serial.println("CAN: 250 kbps | MCP2515 8 MHz | IDs extendidos");
  Serial.println();

  pinMode(CAN_CS_PIN, OUTPUT);
  digitalWrite(CAN_CS_PIN, HIGH);
  SPI.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);

  if (!initCAN()) {
    while (true) {
      delay(2000);
      Serial.println("FATAL: MCP2515 no inicializado. Reiniciar luego de revisar SPI/alimentacion/cristal.");
    }
  }
}

void loop() {
  totalCycles++;
  uint32_t okThisCycle = 0;
  uint32_t failThisCycle = 0;

  Serial.println();
  Serial.println("============================================================");
  Serial.print(" CICLO CAN #"); Serial.println(totalCycles);
  Serial.println("============================================================");

  for (size_t i = 0; i < FRAME_COUNT; i++) {
    if (sendTestFrame(frames[i], i)) okThisCycle++;
    else failThisCycle++;
    delay(20);
  }

  if (failThisCycle == 0) consecutiveFailedCycles = 0;
  else consecutiveFailedCycles++;

  Serial.println("------------------------------------------------------------");
  Serial.print("Resultado ciclo: ");
  Serial.print(okThisCycle); Serial.print(" OK / ");
  Serial.print(failThisCycle); Serial.println(" ERROR");
  printMcpStatus("Estado final:");

  uint8_t eflg = mcpReadRegister(REG_EFLG);
  uint8_t tec = mcpReadRegister(REG_TEC);

  if (failThisCycle == 0 && eflg == 0) {
    Serial.println("RESULTADO GENERADOR: OK - todas las tramas fueron transmitidas sin errores MCP2515.");
  } else {
    Serial.println("RESULTADO GENERADOR: FALLA DETECTADA.");
    if (eflg & 0x20) {
      Serial.println("DIAGNOSTICO: BUS-OFF (TXBO). El MCP2515 dejo de transmitir por demasiados errores.");
    } else if ((eflg & 0x10) || tec >= 128) {
      Serial.println("DIAGNOSTICO: TX ERROR-PASSIVE. Revisar receptor/ACK, bitrate, CANH/CANL y terminacion.");
    } else if ((eflg & 0x04) || tec >= 96) {
      Serial.println("DIAGNOSTICO: muchos errores de transmision. Posible falta de ACK, bitrate incorrecto o problema fisico.");
    } else if (failThisCycle > 0) {
      Serial.println("DIAGNOSTICO: sendMsgBuf fallo. Revisar estado del receptor y bus CAN.");
    }
  }

  if (consecutiveFailedCycles >= 3) {
    Serial.println("AVISO: 3 o mas ciclos consecutivos con fallo. La falla es persistente, no aislada.");
  }

  Serial.print("Acumulado: ciclos="); Serial.print(totalCycles);
  Serial.print(" TX_OK="); Serial.print(totalTxOK);
  Serial.print(" TX_ERROR="); Serial.println(totalTxFail);

  // Pausa para que el receptor pueda cerrar y evaluar el ciclo.
  delay(1200);
}
