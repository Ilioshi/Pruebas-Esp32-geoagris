#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>

// ============================================================
// RECEPTOR CAN DE DIAGNOSTICO - GEOAGRIS
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
//   El receptor queda en NORMAL para generar ACK.
//
// Detecta:
// - ausencia total de trafico valido
// - errores internos MCP2515 (EFLG/TEC/REC)
// - IDs inesperados
// - ID estandar cuando deberia ser extendido
// - DLC incorrecto
// - datos incorrectos
// - orden incorrecto
// - tramas faltantes
// - tramas duplicadas/extras
// - desbordamiento RX
// ============================================================

#define CAN_CS_PIN   5
#define CAN_INT_PIN  21
#define CAN_SCK_PIN  18
#define CAN_MISO_PIN 19
#define CAN_MOSI_PIN 23

MCP_CAN CAN0(CAN_CS_PIN);

static const uint8_t MCP_READ_CMD = 0x03;
static const uint8_t REG_CANSTAT  = 0x0E;
static const uint8_t REG_TEC      = 0x1C;
static const uint8_t REG_REC      = 0x1D;
static const uint8_t REG_CANINTF  = 0x2C;
static const uint8_t REG_EFLG     = 0x2D;

struct ExpectedFrame {
  const char *name;
  uint32_t id;
  uint8_t data[8];
};

const ExpectedFrame expected[] = {
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

const size_t EXPECTED_COUNT = sizeof(expected) / sizeof(expected[0]);

bool cycleActive = false;
uint32_t cycleNumber = 0;
size_t nextExpected = 0;
uint32_t cycleStartMs = 0;
uint32_t lastFrameMs = 0;

uint32_t cycleFrames = 0;
uint32_t cycleCorrect = 0;
uint32_t cycleUnexpected = 0;
uint32_t cycleDlcErrors = 0;
uint32_t cycleDataErrors = 0;
uint32_t cycleSequenceErrors = 0;
uint32_t cycleTypeErrors = 0;
uint32_t cycleMissing = 0;

uint32_t totalFrames = 0;
uint32_t totalGoodCycles = 0;
uint32_t totalBadCycles = 0;
uint32_t totalUnexpected = 0;
uint32_t totalDlcErrors = 0;
uint32_t totalDataErrors = 0;

uint32_t lastAnyFrameMs = 0;
uint32_t lastNoTrafficReportMs = 0;
uint8_t lastREC = 0;
uint8_t lastEFLG = 0;

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

bool sameData(const uint8_t *a, const uint8_t *b, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool frameExactlyMatches(size_t index, uint32_t id, uint8_t dlc, const uint8_t *data) {
  if (index >= EXPECTED_COUNT) return false;
  return id == expected[index].id && dlc == 8 && sameData(data, expected[index].data, 8);
}

int findExactMatch(uint32_t id, uint8_t dlc, const uint8_t *data, size_t startAt) {
  if (dlc != 8) return -1;
  for (size_t i = startAt; i < EXPECTED_COUNT; i++) {
    if (id == expected[i].id && sameData(data, expected[i].data, 8)) return (int)i;
  }
  // Permite reconocer duplicados o tramas que volvieron hacia atras.
  for (size_t i = 0; i < startAt && i < EXPECTED_COUNT; i++) {
    if (id == expected[i].id && sameData(data, expected[i].data, 8)) return (int)i;
  }
  return -1;
}

bool idIsExpected(uint32_t id) {
  for (size_t i = 0; i < EXPECTED_COUNT; i++) {
    if (expected[i].id == id) return true;
  }
  return false;
}

void resetCycleCounters() {
  nextExpected = 0;
  cycleFrames = 0;
  cycleCorrect = 0;
  cycleUnexpected = 0;
  cycleDlcErrors = 0;
  cycleDataErrors = 0;
  cycleSequenceErrors = 0;
  cycleTypeErrors = 0;
  cycleMissing = 0;
}

void finishCycle(const char *reason) {
  if (!cycleActive) return;

  if (nextExpected < EXPECTED_COUNT) {
    cycleMissing += (EXPECTED_COUNT - nextExpected);
  }

  uint8_t eflg = mcpReadRegister(REG_EFLG);
  uint8_t rec = mcpReadRegister(REG_REC);
  bool hwError = (eflg != 0);

  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.print("CIERRE CICLO #"); Serial.print(cycleNumber);
  Serial.print(" ("); Serial.print(reason); Serial.println(")");
  Serial.print("Frames vistos: "); Serial.println(cycleFrames);
  Serial.print("Correctos: "); Serial.print(cycleCorrect); Serial.print('/'); Serial.println(EXPECTED_COUNT);
  Serial.print("Faltantes: "); Serial.println(cycleMissing);
  Serial.print("ID/datos inesperados: "); Serial.println(cycleUnexpected);
  Serial.print("DLC incorrecto: "); Serial.println(cycleDlcErrors);
  Serial.print("Datos incorrectos: "); Serial.println(cycleDataErrors);
  Serial.print("Orden/duplicados: "); Serial.println(cycleSequenceErrors);
  Serial.print("Tipo ID incorrecto: "); Serial.println(cycleTypeErrors);
  printMcpStatus("MCP2515:");

  bool ok = (cycleCorrect == EXPECTED_COUNT &&
             cycleMissing == 0 &&
             cycleUnexpected == 0 &&
             cycleDlcErrors == 0 &&
             cycleDataErrors == 0 &&
             cycleSequenceErrors == 0 &&
             cycleTypeErrors == 0 &&
             !hwError);

  if (ok) {
    totalGoodCycles++;
    Serial.println("RESULTADO CICLO: OK PERFECTO - llegaron las 14 tramas exactas, en orden y sin errores MCP2515.");
  } else {
    totalBadCycles++;
    Serial.println("RESULTADO CICLO: FALLA DETECTADA.");

    if (eflg & 0xC0) {
      Serial.println("DIAGNOSTICO: OVERFLOW RX. Llegan tramas mas rapido de lo que se estan leyendo o el programa queda bloqueado.");
    }
    if ((eflg & 0x08) || rec >= 128) {
      Serial.println("DIAGNOSTICO: RX ERROR-PASSIVE. Hay actividad CAN pero muchas tramas tienen errores fisicos/temporales.");
    } else if ((eflg & 0x02) || rec >= 96) {
      Serial.println("DIAGNOSTICO: contador REC alto. Posible bitrate/cristal incorrecto, ruido, terminacion o cableado CANH/CANL.");
    }
    if (cycleDlcErrors > 0) {
      Serial.println("DIAGNOSTICO: al menos una trama llego con DLC distinto de 8.");
    }
    if (cycleDataErrors > 0) {
      Serial.println("DIAGNOSTICO: ID esperado pero payload distinto al patron del generador.");
    }
    if (cycleSequenceErrors > 0) {
      Serial.println("DIAGNOSTICO: hubo tramas fuera de orden, repetidas o se saltaron posiciones esperadas.");
    }
    if (cycleUnexpected > 0) {
      Serial.println("DIAGNOSTICO: se recibio trafico que no pertenece al generador de prueba.");
    }
    if (cycleMissing > 0 && eflg == 0) {
      Serial.println("DIAGNOSTICO: faltaron tramas aunque el MCP2515 no reporta error fisico en este momento.");
    }
  }

  Serial.print("Acumulado: buenos="); Serial.print(totalGoodCycles);
  Serial.print(" malos="); Serial.print(totalBadCycles);
  Serial.print(" frames="); Serial.println(totalFrames);
  Serial.println("------------------------------------------------------------");

  cycleActive = false;
}

void startNewCycle() {
  if (cycleActive) finishCycle("llego el inicio del ciclo siguiente");

  cycleActive = true;
  cycleNumber++;
  cycleStartMs = millis();
  resetCycleCounters();

  Serial.println();
  Serial.println("============================================================");
  Serial.print(" NUEVO CICLO CAN #"); Serial.println(cycleNumber);
  Serial.println("============================================================");
}

bool initCAN() {
  Serial.println("Inicializando MCP2515...");
  for (int attempt = 1; attempt <= 5; attempt++) {
    byte result = CAN0.begin(MCP_ANY, CAN_250KBPS, MCP_8MHZ);
    if (result == CAN_OK) {
      CAN0.setMode(MCP_NORMAL); // IMPORTANTE: asi el receptor genera ACK.
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

void processFrame(uint32_t id, uint8_t ext, uint8_t dlc, const uint8_t *data) {
  totalFrames++;
  lastAnyFrameMs = millis();

  // El RPM es la primera trama de cada ciclo y permite sincronizar.
  bool isStartFrame = (id == expected[0].id && dlc == 8 && sameData(data, expected[0].data, 8));
  if (isStartFrame) startNewCycle();

  if (!cycleActive) {
    Serial.print("RX fuera de ciclo: ID=0x"); Serial.print(id, HEX);
    Serial.print(" EXT="); Serial.print(ext);
    Serial.print(" DLC="); Serial.print(dlc);
    Serial.print(" DATA="); printData(data, dlc); Serial.println();
    totalUnexpected++;
    return;
  }

  cycleFrames++;
  lastFrameMs = millis();

  Serial.print("RX #"); Serial.print(cycleFrames);
  Serial.print(" ID=0x"); Serial.print(id, HEX);
  Serial.print(" EXT="); Serial.print(ext);
  Serial.print(" DLC="); Serial.print(dlc);
  Serial.print(" DATA="); printData(data, dlc);

  if (ext != 1) {
    cycleTypeErrors++;
    Serial.println("  -> ERROR: deberia ser ID extendido");
    return;
  }

  if (dlc != 8) {
    cycleDlcErrors++;
    totalDlcErrors++;
    Serial.println("  -> ERROR: DLC incorrecto");
    return;
  }

  if (nextExpected < EXPECTED_COUNT && frameExactlyMatches(nextExpected, id, dlc, data)) {
    Serial.print("  -> OK: "); Serial.println(expected[nextExpected].name);
    cycleCorrect++;
    nextExpected++;
    return;
  }

  int match = findExactMatch(id, dlc, data, nextExpected);
  if (match >= 0) {
    cycleSequenceErrors++;

    if ((size_t)match > nextExpected) {
      size_t skipped = (size_t)match - nextExpected;
      cycleMissing += skipped;
      Serial.print("  -> ERROR ORDEN: esperaba ");
      Serial.print(expected[nextExpected].name);
      Serial.print(", llego "); Serial.print(expected[match].name);
      Serial.print(". Se saltaron "); Serial.print(skipped); Serial.println(" trama(s).");
      cycleCorrect++;
      nextExpected = (size_t)match + 1;
    } else {
      Serial.print("  -> ERROR: trama repetida/fuera de orden: "); Serial.println(expected[match].name);
    }
    return;
  }

  if (idIsExpected(id)) {
    cycleDataErrors++;
    totalDataErrors++;
    Serial.println("  -> ERROR: ID conocido pero DATA no coincide con ninguna trama esperada");
  } else {
    cycleUnexpected++;
    totalUnexpected++;
    Serial.println("  -> ERROR: ID inesperado / trafico ajeno a la prueba");
  }
}

void noTrafficDiagnostic() {
  uint32_t now = millis();
  if (now - lastAnyFrameMs < 3000) return;
  if (now - lastNoTrafficReportMs < 3000) return;
  lastNoTrafficReportMs = now;

  uint8_t rec = mcpReadRegister(REG_REC);
  uint8_t eflg = mcpReadRegister(REG_EFLG);

  Serial.println();
  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  Serial.println("SIN TRAMAS CAN VALIDAS DURANTE 3 SEGUNDOS");
  printMcpStatus("MCP2515:");

  if ((eflg & 0x0A) || rec > lastREC) {
    Serial.println("DIAGNOSTICO: el receptor ve errores CAN aunque no entrega tramas validas.");
    Serial.println("Posibles causas: bitrate distinto, cristal MCP2515 incorrecto, ruido, CANH/CANL, terminacion.");
  } else {
    Serial.println("DIAGNOSTICO: no hay tramas validas ni aumento claro de errores RX.");
    Serial.println("Revisar que el generador este transmitiendo, alimentacion, transceiver, CANH/CANL y masa comun si corresponde.");
  }

  if (eflg & 0xC0) {
    Serial.println("ADEMAS: hay OVERFLOW de RX en el MCP2515.");
  }

  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  lastREC = rec;
  lastEFLG = eflg;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" RECEPTOR CAN - DIAGNOSTICO COMPLETO");
  Serial.println("============================================================");
  Serial.println("ESP32 SPI: SCK=18 MISO=19 MOSI=23 CS=5 INT=21");
  Serial.println("CAN: 250 kbps | MCP2515 8 MHz | modo NORMAL para ACK");
  Serial.println();

  pinMode(CAN_CS_PIN, OUTPUT);
  digitalWrite(CAN_CS_PIN, HIGH);
  pinMode(CAN_INT_PIN, INPUT);
  SPI.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);

  if (!initCAN()) {
    while (true) {
      delay(2000);
      Serial.println("FATAL: MCP2515 no inicializado. Revisar SPI, alimentacion y frecuencia del cristal.");
    }
  }

  lastAnyFrameMs = millis();
}

void loop() {
  // Vaciar todos los mensajes pendientes para evitar overflow.
  while (CAN0.checkReceive() == CAN_MSGAVAIL) {
    uint32_t rxId = 0;
    uint8_t ext = 0;
    uint8_t len = 0;
    uint8_t buf[8] = {0};

    byte stat = CAN0.readMsgBuf(&rxId, &ext, &len, buf);
    if (stat == CAN_OK) {
      processFrame(rxId, ext, len, buf);
    } else {
      Serial.print("ERROR readMsgBuf(): codigo="); Serial.println(stat);
      printMcpStatus("MCP2515:");
    }
  }

  // Si un ciclo empezo pero se corto y no llego el siguiente inicio,
  // cerrarlo por timeout.
  if (cycleActive && (millis() - lastFrameMs > 700)) {
    finishCycle("timeout: dejaron de llegar tramas");
  }

  noTrafficDiagnostic();
  delay(1);
}
