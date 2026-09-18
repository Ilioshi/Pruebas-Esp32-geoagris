#include <Arduino.h>
#include <BluetoothSerial.h>
#include <SPI.h>
#include <mcp_can.h>

/*
  SIMULADOR TRAX COMPLETO - CORREGIDO

  ESP simuladora:
    TR0 del DUT (TTL2) <-> UART0 remapeada: RX32 / TX33
    TR1 del DUT (TTL1) <-> UART2:             RX25 / TX26
    RS485 clima: RX16 / TX17 / DE-RE4
    CAN MCP2515: CS5, SCK18, MISO19, MOSI23, 250 kbps, cristal 8 MHz

  IMPORTANTE:
  - Cruce TX con RX entre ambas ESP y unir GND.
  - No conectar USB al DUT mientras TTL2/TR0 está conectado.
  - El monitor es Bluetooth clásico: "TRAX SIMULATOR".
*/

BluetoothSerial SerialBT;

class DebugSerial : public Print {
public:
  void begin(uint32_t baud) {
    ::Serial.begin(baud);
    SerialBT.begin("TRAX SIMULATOR");
  }

  size_t write(uint8_t c) override {
    if (SerialBT.hasClient()) SerialBT.write(c);
    return 1;
  }

  using Print::write;
};

DebugSerial Debug;
#define Serial Debug

// ---------------------------------------------------------------------------
// DOS PUERTOS DEL TRAX SIMULADO
// ---------------------------------------------------------------------------

HardwareSerial TTL2(0);       // TR0 del DUT: canal de servicio del firmware.
HardwareSerial TraxSerial(2); // TR1 del DUT: puente BLE/app.

#define TTL2_RX   32
#define TTL2_TX   33
#define TR1_RX    25
#define TR1_TX    26
#define TRAX_BAUD 115200

const char* TRAX_ID = "2875246";

String tr0RxBuffer;
String tr1RxBuffer;
String tr0DutDebugBuffer;
String tr1DutDebugBuffer;
HardwareSerial* responsePort = nullptr;
const char* responseChannel = "SIN_CANAL";

// Contadores y marcas de tiempo para que el monitor Bluetooth sirva como
// evidencia de diagnóstico, no sólo como un volcado de tramas.
unsigned long tr0Frames = 0, tr1Frames = 0;
unsigned long tr0Responses = 0, tr1Responses = 0;
unsigned long traxUnknownCommands = 0;
unsigned long lastTr0FrameTime = 0, lastTr1FrameTime = 0;
unsigned long lastTr1Qus07Time = 0;
unsigned long lastRs485QueryTime = 0;

// Evita inundar Bluetooth: cambiar a 1 solo para depurar CAN frame por frame.
#define CAN_VERBOSE 0

// ---------------------------------------------------------------------------
// ESTADO TRAX
// ---------------------------------------------------------------------------

bool simQSG01 = true;
bool simQSG02 = true;
bool simQSG03 = true;
bool simQSG04 = true;
bool simQSG05 = true;
float simSpeedKmh = 7.0f;
unsigned long lastRUS07StateChange = 0;

int simBatteryTenths = 124;
unsigned long lastBatteryChange = 0;

// Payload puro, sin la coma separadora: 5+4+5+4+5 = 23 caracteres.
// Nunca se carga artificialmente: sólo processSTX() lo actualiza cuando la
// ESP real envía STX08 por TR0 después de decodificar CAN.
String tx08Buffer = "-----------------------";
unsigned long lastStx08Time = 0;

void logLine(const String& text) {
  Serial.println(text);
}

uint8_t calculateChecksum(const String& frame) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < frame.length(); ++i) checksum ^= (uint8_t)frame[i];
  return checksum;
}

String makeResponse(const String& body) {
  String frame = ">";
  frame += body;
  frame += ";ID=";
  frame += TRAX_ID;
  frame += ";*";
  char hex[3];
  sprintf(hex, "%02X", calculateChecksum(frame));
  frame += hex;
  frame += "<";
  return frame;
}

// Siempre responde en el mismo puerto que originó el comando.
void sendTrax(const String& response) {
  if (responsePort == nullptr) {
    logLine("[FALLO TRAX] No hay puerto de respuesta seleccionado. Posible error interno del simulador.");
    return;
  }
  if (responsePort == &TTL2) ++tr0Responses;
  else if (responsePort == &TraxSerial) ++tr1Responses;
  logLine(String("[TRAX ") + responseChannel + " TX] " + response);
  responsePort->print(response);
}

void serviceRUSState() {
  const unsigned long now = millis();

  if (now - lastRUS07StateChange >= 5000) {
    lastRUS07StateChange = now;
    simQSG01 = !simQSG01;
    simQSG05 = !simQSG05;
    simSpeedKmh = (simSpeedKmh >= 11.0f) ? 0.0f : simSpeedKmh + 2.0f;
  }

  if (now - lastBatteryChange >= 10000) {
    lastBatteryChange = now;
    simBatteryTenths = (simBatteryTenths >= 126) ? 120 : simBatteryTenths + 2;
  }
}

String buildRUS07() {
  /*
    El firmware 2.0.3 exige:
      - 12 dígitos de fecha/hora desde offset 7.
      - rumbo numérico en su offset fijo.
      - velocidad entre la tercera y cuarta coma.
    Esta base es compatible con el ejemplo interno de trax_utils.cpp.
  */
  String body =
    "RUS07,10092614500000003419.5533S06012.0261W311E70000000000-125000.0-0400000000001,76.6,";
  body += String(simSpeedKmh, 1);
  body += ",99";
  return body;
}

/*
  La aplicación Cab no interpreta RUS07 igual que trax_utils.cpp:
  espera el reporte configurable SUC07, que contiene QSG01..05, QTX07
  y QTX08. Por eso la respuesta TR1 incluye TX08; el firmware por TR0
  conserva el formato estricto que necesita para GPS.
*/
String buildRUS07ForApp() {
  String qsg;
  qsg += simQSG01 ? "1" : "0";
  qsg += simQSG02 ? "6" : "0";
  qsg += simQSG03 ? "9" : "0";
  qsg += simQSG04 ? "1" : "0";
  qsg += simQSG05 ? "3" : "0";

  /*
    SUC07 define primero QTX07 desde posición 7 y con longitud 47,
    seguido por QTX08 con longitud 23. La aplicación usa esos offsets
    absolutos; si se anteponen los SG, todos los datos CAN quedan corridos.
  */
  const String qtx07 = "10092614463117413435.9443S05825.3405W0000700000";
  String body = "RUS07,";
  body += qtx07;
  body += tx08Buffer;  // RPM, torque, consumo, temperatura y horas.
  body += qsg;
  body += ",41.8,";
  body += String(simSpeedKmh, 1);
  body += ",A00000000,00";
  return body;
}

String buildRUS08() {
  String body =
    "RUS08,1009261445480001697000815290000002310000000000403000010-----------.-893144040014063130500";
  char battery[5];
  sprintf(battery, "%04d", simBatteryTenths);
  // Mantiene la ubicación usada por la captura original.
  return body.substring(0, 52) + String(battery) + body.substring(56);
}

// ---------------------------------------------------------------------------
// PROTOCOLO TRAX
// ---------------------------------------------------------------------------

void processQUS(const String& command) {
  if (command == ">QUS0A<") {
    sendTrax(makeResponse("RUS0A,100926144507,e32:connection established 2.0.3"));
  } else if (command == ">QUS0B<") {
    // Esta respuesta permite que la app deje de mostrar "unknown".
    sendTrax(makeResponse("RUS0B,5505,josiasprueba25,20.00,34,Geoagris,137,Pulverizadora,6,29817476,29817472"));
  } else if (command == ">QUS0C<") {
    sendTrax(makeResponse("RUS0C,100.0,2,,,,,99.0,,,,,"));
  } else if (command == ">QUS0D<") {
    sendTrax(makeResponse("RUS0D,,,,,,,,,,"));
  } else if (command == ">QUS0E<") {
    sendTrax(makeResponse("RUS0E"));
  } else if (command == ">QUS0F<") {
    // Sin credenciales Wi-Fi simuladas.
    sendTrax(makeResponse("RUS0F"));
  } else if (command == ">QUS01<") {
    sendTrax(makeResponse("RUS01"));
  } else if (command == ">QUS02<") {
    sendTrax(makeResponse("RUS02,100926144534,STOP"));
  } else if (command == ">QUS03<") {
    sendTrax(makeResponse("RUS03"));
  } else if (command == ">QUS04<") {
    sendTrax(makeResponse("RUS04,100926144537,HAN:c8k71_AwTR__AN"));
  } else if (command == ">QUS05<") {
    sendTrax(makeResponse("RUS05,100926144538,IS2:000A0A1C021A:~:~:~:000045E9:~:~:~:~:0000C86C"));
  } else if (command == ">QUS06<") {
    sendTrax(makeResponse("RUS06"));
  } else if (command == ">QUS07<") {
    if (responsePort == &TraxSerial) {
      sendTrax(makeResponse(buildRUS07ForApp()));
    } else {
      sendTrax(makeResponse(buildRUS07()));
    }
  } else if (command == ">QUS08<") {
    sendTrax(makeResponse(buildRUS08()));
  } else if (command == ">QUS09<") {
    sendTrax(makeResponse("RUS09,100926144549,IS1:000A0A1C021A:EC84F66C:DBFBC2CA:00F0:0001:0000:0000C86C:~:~:~:1"));
  } else {
    ++traxUnknownCommands;
    logLine("[FALLO TRAX] QUS no implementado: " + command +
            ". La app/firmware pidió una función que este simulador no conoce.");
  }
}

void processQTX(const String& command) {
  if (command == ">QTX00<") {
    sendTrax(makeResponse("RTX00,$T1:---.-"));
  } else if (command == ">QTX01<") {
    sendTrax(makeResponse("RTX01,$GPGGA,141731.00,3435.944363,S,05825.340530,W,1,08,0.8,41.8,M,23.0,M,,*5B"));
  } else if (command == ">QTX02<") {
    sendTrax(makeResponse("RTX02,e32:connection established 2.0.3"));
  } else if (command == ">QTX03<") {
    sendTrax(makeResponse("RTX03,IS1:000A0A1C021A:EC84F66C:DBFBC2CA:00F0:0001:0000:0000C86C:~:~:~:1"));
  } else if (command == ">QTX04<") {
    sendTrax(makeResponse("RTX04,$GPRMC,141731.00,A,3435.944363,S,05825.340530,W,0.0,,100926,5.5,W,A*0E"));
  } else if (command == ">QTX05<") {
    sendTrax(makeResponse("RTX05,"));
  } else if (command == ">QTX06<") {
    sendTrax(makeResponse("RTX06,HAN:c8k71_AwTR__AN"));
  } else if (command == ">QTX07<") {
    sendTrax(makeResponse("RTX07,10092614463117413435.9443S05825.3405W0000700000"));
  } else if (command == ">QTX08<") {
    sendTrax(makeResponse(String("RTX08,") + tx08Buffer));
  } else if (command == ">QTX09<") {
    sendTrax(makeResponse("RTX09,"));
  } else if (command == ">QTX0A<" || command == ">QTX0B<" ||
             command == ">QTX0C<" || command == ">QTX0E<" ||
             command == ">QTX0F<") {
    sendTrax(makeResponse("ETX PARAM"));
  } else if (command == ">QTX0D<") {
    sendTrax(makeResponse("RTX0D"));
  } else if (command == ">QTX10<") {
    sendTrax(makeResponse("RTX10,----"));
  } else if (command == ">QTX11<") {
    sendTrax(makeResponse("RTX11,----"));
  } else if (command == ">QTX12<") {
    sendTrax(makeResponse("RTX12,"));
  } else if (command == ">QTX13<") {
    sendTrax(makeResponse("RTX13,IS2:000A0A1C021A:~:~:~:000045E9:~:~:~:~:0000C86C"));
  } else if (command == ">QTX14<") {
    sendTrax(makeResponse("RTX14,STOP"));
  } else if (command == ">QTX15<") {
    sendTrax(makeResponse("RTX15,100926145000;3;Do not spray;1;5.3;ok;195;1;24.0;ok;61;3;33;Very High;NW;315;0;0"));
  } else {
    ++traxUnknownCommands;
    logLine("[FALLO TRAX] QTX no implementado: " + command +
            ". Registrar esta trama para agregar la respuesta correcta.");
  }
}

bool isKnownSTX(const String& id) {
  return id == "02" || id == "03" || id == "06" || id == "08" ||
         id == "09" || id == "13" || id == "15";
}

bool isNumericField(const String& value) {
  if (value.length() == 0) return false;
  for (size_t i = 0; i < value.length(); ++i)
    if (value[i] < '0' || value[i] > '9') return false;
  return true;
}

void processSTX08Diagnostic(const String& payload) {
  logLine("[STX08] payload=" + payload);
  if (payload.length() != 23) {
    logLine("[STX08] ERROR longitud: " + String(payload.length()) + " (se esperaban 23)");
    return;
  }

  const String rpm = payload.substring(0, 5);
  const String torque = payload.substring(5, 9);
  const String fuel = payload.substring(9, 14);
  const String temp = payload.substring(14, 18);
  const String hours = payload.substring(18, 23);
  logLine("[STX08] rpm=" + rpm + " torque=" + torque +
          " fuel=" + fuel + " temp=" + temp + " hours=" + hours);
}

void processSTX(const String& command) {
  if (command.length() < 7) return;

  const String id = command.substring(4, 6);
  String payload = command.substring(6);
  if (payload.endsWith("<")) payload.remove(payload.length() - 1);
  if (payload.startsWith(",")) payload.remove(0, 1);

  if (!isKnownSTX(id)) {
    ++traxUnknownCommands;
    logLine("[FALLO TRAX] STX" + id + " no implementado. Se recibió dato de la ESP real pero no se confirmó.");
    return;
  }

  if (id == "08") {
    if (payload.length() == 23) {
      tx08Buffer = payload;
      lastStx08Time = millis();
      logLine("[STX08] TX08 actualizado");
    }
    processSTX08Diagnostic(payload);
  }

  String response = "RTX";
  response += id;
  response += ",";
  response += payload;
  sendTrax(makeResponse(response));
}

void processCommand(const String& command, HardwareSerial& sourcePort) {
  responsePort = &sourcePort;
  logLine(String("[TRAX ") + responseChannel + " RX] " + command);

  if (command == ">QID<") {
    sendTrax(makeResponse("RID2875246,SHOW,"));
  } else if (command.startsWith(">QUS")) {
    processQUS(command);
  } else if (command.startsWith(">QTX")) {
    processQTX(command);
  } else if (command.startsWith(">SP")) {
    // La aplicación oculta la clave en su log y hay variantes de firmware
    // que envían SP/SPW. En el banco de pruebas se acepta cualquiera para
    // que la autenticación no sea el motivo de cortar la conexión.
    sendTrax(makeResponse("RPWgeo18ris"));
  } else if (command.startsWith(">STX")) {
    processSTX(command);
  } else if (command.startsWith(">SLD") || command.startsWith(">SSD") ||
             command.startsWith(">SRS") || command.startsWith(">SAK") ||
             command.startsWith(">SCC")) {
    // Comandos de control que el firmware puede emitir en modo Wi-Fi/BLE.
    String body = "R";
    body += command.substring(1, command.length() - 1);
    sendTrax(makeResponse(body));
  } else {
    ++traxUnknownCommands;
    logLine("[FALLO TRAX] Comando no implementado: " + command +
            ". Posible diferencia de firmware/configuración TRAX.");
  }
}

void readTraxPort(HardwareSerial& port, String& buffer, String& debugBuffer,
                  const char* channel) {
  while (port.available()) {
    const char c = static_cast<char>(port.read());

    // Canal lateral de diagnóstico de la ESP real. Usa líneas que empiezan
    // por #DUT y no se reenvían al TRAX ni se confunden con >...<.
    if (buffer.length() == 0 && debugBuffer.length() > 0) {
      if (c == '\n') {
        logLine(String("[DUT ") + channel + "] " + debugBuffer);
        debugBuffer = "";
        continue;
      }
      if (c != '\r') {
        if (debugBuffer.length() < 240) debugBuffer += c;
        else {
          logLine(String("[DUT ") + channel + "] ERROR: diagnóstico demasiado largo");
          debugBuffer = "";
        }
      }
      continue;
    }

    if (buffer.length() == 0 && c == '#') {
      debugBuffer = "#";
      continue;
    }

    if (c == '>') {
      buffer = ">";
      continue;
    }
    if (buffer.length() == 0) continue;
    buffer += c;

    if (c == '<') {
      const String command = buffer;
      buffer = "";
      responseChannel = channel;
      if (&port == &TTL2) {
        ++tr0Frames;
        lastTr0FrameTime = millis();
      } else {
        ++tr1Frames;
        lastTr1FrameTime = millis();
        if (command == ">QUS07<") lastTr1Qus07Time = millis();
      }
      processCommand(command, port);
    }
    if (buffer.length() > 1024) {
      buffer = "";
      logLine(String("[FALLO UART ") + channel +
              "] Trama >1024 bytes sin cierre '<'. Posibles causas: baud incorrecto, ruido, GND ausente o datos corruptos.");
    }
  }
}

// ---------------------------------------------------------------------------
// CAN
// ---------------------------------------------------------------------------

#define CAN_CS_PIN   5
#define CAN_SCK_PIN  18
#define CAN_MISO_PIN 19
#define CAN_MOSI_PIN 23

MCP_CAN CAN0(CAN_CS_PIN);
bool canReady = false;

byte rpmData[8]                = {0x00, 0x00, 0x80, 0xF0, 0x29, 0x00, 0x00, 0x00};
byte consumoCombustibleData[8] = {0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
byte temperaturaMotorData[8]   = {0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
byte horasMotorData[8]         = {0xAB, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

byte coordinate[8]     = {0x6C, 0xF6, 0x84, 0xEC, 0xCA, 0xC2, 0xFB, 0xDB};
byte datatime[8]       = {0x00, 0x36, 0x11, 0x02, 0x50, 0x19, 0x00, 0x00};
byte speedDirection[8] = {0x32, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00};
byte setpointVolume[8] = {0x13, 0x00, 0x01, 0x00, 0x60, 0xE3, 0x16, 0x00};
byte volume[8]         = {0x13, 0x00, 0x02, 0x00, 0x60, 0xE3, 0x16, 0x00};
byte workstate[8]      = {0x00, 0x00, 0x8D, 0x00, 0x01, 0x00, 0x00, 0x00};
byte setpointmassa[8]  = {0x13, 0x00, 0x06, 0x00, 0xC8, 0xAF, 0x00, 0x00};
byte massa[8]          = {0x33, 0x13, 0x07, 0x00, 0xC8, 0xAF, 0x00, 0x00};
byte pumpPressure[8]   = {0x13, 0x00, 0xC2, 0x00, 0xE0, 0x70, 0x72, 0x00};
byte sections[8]       = {0x33, 0x00, 0xA1, 0x00, 0x55, 0x55, 0x01, 0x00};

struct CanFrameDef { unsigned long id; byte* data; };
CanFrameDef canFrames[] = {
  {0x0CF00480UL, rpmData}, {0x18FEF280UL, consumoCombustibleData},
  {0x18FEEE80UL, temperaturaMotorData}, {0x18FEE580UL, horasMotorData},
  {0x0CFEF300UL, coordinate}, {0x0CFEE600UL, datatime},
  {0x0CFEE800UL, speedDirection}, {0x0CCB0000UL, setpointVolume},
  {0x0CCB0000UL, volume}, {0x0CCB0000UL, workstate},
  {0x18CBF700UL, setpointmassa}, {0x18CB0000UL, massa},
  {0x18CB0000UL, pumpPressure}, {0x18CB0000UL, sections}
};

const uint8_t CAN_FRAME_COUNT = sizeof(canFrames) / sizeof(canFrames[0]);
uint8_t canFrameIndex = 0;
unsigned long canOkCount = 0, canErrorCount = 0, canRxCount = 0;
unsigned long lastCanFrameTime = 0, lastCanStatusTime = 0;

void initCAN() {
  SPI.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);
  delay(20);
  const byte result = CAN0.begin(MCP_ANY, CAN_250KBPS, MCP_8MHZ);
  canReady = (result == CAN_OK);
  if (!canReady) {
    logLine("[FALLO CAN] MCP2515 no inicializa; código=" + String(result) +
            ". Revisar CS=5, SPI 18/19/23, alimentación, masa y cristal de 8 MHz.");
    return;
  }
  CAN0.setMode(MCP_NORMAL);
  CAN0.enOneShotTX();
  logLine("[CAN OK] MCP2515 inicializado: 250 kbps, cristal 8 MHz, modo NORMAL.");
}

void sendOneCANFrame() {
  if (!canReady) return;
  CanFrameDef& frame = canFrames[canFrameIndex];
  const byte result = CAN0.sendMsgBuf(frame.id, 1, 8, frame.data);
  if (result == CAN_OK) {
    ++canOkCount;
    #if CAN_VERBOSE
      logLine("[CAN] 0x" + String(frame.id, HEX) + " OK");
    #endif
  } else {
    ++canErrorCount;
    logLine("[FALLO CAN] TX ID=0x" + String(frame.id, HEX) + " código=" + String(result) +
            ". Posibles causas: CANH/CANL invertidos o abiertos, sin otro nodo/ACK, velocidad distinta o transceptor sin alimentación.");
    CAN0.abortTX();
  }
  canFrameIndex = (canFrameIndex + 1) % CAN_FRAME_COUNT;
}

void readCAN() {
  if (!canReady) return;
  while (CAN0.checkReceive() == CAN_MSGAVAIL) {
    unsigned long id = 0;
    byte len = 0, data[8];
    CAN0.readMsgBuf(&id, &len, data);
    ++canRxCount;
  }
}

void serviceCAN() {
  if (!canReady) return;
  const unsigned long now = millis();
  if (now - lastCanFrameTime >= 14) {
    lastCanFrameTime = now;
    sendOneCANFrame();
  }
  if (now - lastCanStatusTime >= 10000) {
    lastCanStatusTime = now;
    if (canErrorCount == 0 && canOkCount > 0) {
      logLine("[CAN OK] TX=" + String(canOkCount) + " ERR=0 RX=" + String(canRxCount) +
              ". Hay ACK en el bus; esto prueba comunicación eléctrica con al menos un nodo, no que el firmware haya interpretado los PGN.");
    } else {
      logLine("[FALLO CAN] TX_OK=" + String(canOkCount) + " TX_ERR=" + String(canErrorCount) +
              " RX=" + String(canRxCount) + ". Ver errores anteriores para la causa concreta.");
    }
  }
}

// ---------------------------------------------------------------------------
// RS485 / MODBUS: igual a la prueba original
// ---------------------------------------------------------------------------

#define RS485_TX_PIN     17
#define RS485_RX_PIN     16
#define RS485_DE_RE_PIN  4
#define MODBUS_SLAVE_ID  0xFF

HardwareSerial RS485(1);
const uint16_t REG_TEMP = 0x18B7, REG_HUMIDITY = 0x0BA3, REG_PRESSURE = 0x279D;
const uint16_t REG_WIND_SPEED = 0x007E, REG_WIND_DIR = 0x0D83, REG_RAIN = 0x0032;
const uint16_t REG_COMPASS = 0x0045;

uint8_t weatherRequest[8];
uint8_t weatherRequestIndex = 0;
unsigned long totalQueries = 0, totalResponses = 0, totalWeatherErrors = 0;

uint16_t ModRTU_CRC(const uint8_t* buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; ++pos) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 0; i < 8; ++i) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

bool getRegisterValue(uint16_t address, uint16_t& value) {
  switch (address) {
    case 0x0009: value = REG_TEMP; return true;
    case 0x000A: value = REG_HUMIDITY; return true;
    case 0x000B: value = REG_PRESSURE; return true;
    case 0x000C: value = REG_WIND_SPEED; return true;
    case 0x000D: value = REG_WIND_DIR; return true;
    case 0x000E: value = REG_RAIN; return true;
    case 0x0020: value = REG_COMPASS; return true;
    default: return false;
  }
}

void sendWeatherResponse(uint8_t slave, uint8_t function, const uint8_t* data, uint8_t dataLength) {
  uint8_t response[256];
  int length = 0;
  response[length++] = slave;
  response[length++] = function;
  response[length++] = dataLength;
  for (uint8_t i = 0; i < dataLength; ++i) response[length++] = data[i];
  const uint16_t crc = ModRTU_CRC(response, length);
  response[length++] = crc & 0xFF;
  response[length++] = crc >> 8;
  digitalWrite(RS485_DE_RE_PIN, HIGH);
  RS485.write(response, length);
  RS485.flush();
  digitalWrite(RS485_DE_RE_PIN, LOW);
  ++totalResponses;
}

void processWeatherRequest(uint8_t* request) {
  const uint8_t slave = request[0], function = request[1];
  const uint16_t firstRegister = ((uint16_t)request[2] << 8) | request[3];
  const uint16_t numRegisters = ((uint16_t)request[4] << 8) | request[5];
  ++totalQueries;
  lastRs485QueryTime = millis();

  const uint16_t crcReceived = ((uint16_t)request[7] << 8) | request[6];
  const bool expected = (firstRegister == 0x0009 && numRegisters == 6) ||
                        (firstRegister == 0x0020 && numRegisters == 1);
  if (slave != MODBUS_SLAVE_ID || function != 0x03 || !expected ||
      ModRTU_CRC(request, 6) != crcReceived) {
    ++totalWeatherErrors;
    logLine("[FALLO RS485] Consulta inválida: slave=0x" + String(slave, HEX) +
            " función=0x" + String(function, HEX) + " registro=0x" + String(firstRegister, HEX) +
            " cantidad=" + String(numRegisters) +
            ". Posibles causas: baud/paridad distintos, ruido, CRC corrupto o firmware consultando otro mapa Modbus.");
    return;
  }

  uint8_t data[12];
  uint8_t dataLength = 0;
  for (uint16_t i = 0; i < numRegisters; ++i) {
    uint16_t value;
    if (!getRegisterValue(firstRegister + i, value)) {
      ++totalWeatherErrors;
      return;
    }
    data[dataLength++] = value >> 8;
    data[dataLength++] = value & 0xFF;
  }
  sendWeatherResponse(slave, function, data, dataLength);
  logLine("[RS485 OK] Consulta #" + String(totalQueries) + " registro=0x" +
          String(firstRegister, HEX) + " x" + String(numRegisters) + "; respuesta enviada.");
}

void readWeather() {
  while (RS485.available()) {
    const uint8_t byteIn = RS485.read();
    if (weatherRequestIndex == 0 && byteIn != MODBUS_SLAVE_ID) continue;
    weatherRequest[weatherRequestIndex++] = byteIn;
    if (weatherRequestIndex == 8) {
      processWeatherRequest(weatherRequest);
      weatherRequestIndex = 0;
    }
  }
}

// El monitor Bluetooth es una herramienta de diagnóstico. No repetimos el
// mismo estado cada 10 s: las tramas RX/TX ya se registran en el instante en
// que suceden. Sólo se imprime un diagnóstico completo al arrancar, ante un
// cambio de estado o como resumen de seguimiento cada minuto.
const unsigned long DIAGNOSTIC_HEARTBEAT_MS = 60000UL;
unsigned long lastStatusTime = 0;
String lastDiagnosticSignature;

String elapsedText(unsigned long stamp) {
  if (stamp == 0) return "nunca";
  return String((millis() - stamp) / 1000) + " s";
}

void showStatus() {
  String tr1State = (tr1Frames == 0) ? "SIN_TRAMAS" :
                    ((lastTr1Qus07Time == 0) ? "ENLACE_PARCIAL" : "APP_OK");
  String tr0State = (tr0Frames == 0) ? "SIN_TRAMAS" :
                    ((lastStx08Time == 0) ? "SIN_STX08" : "STX08_OK");
  String canState = !canReady ? "FALLA" :
                    ((canOkCount == 0) ? "SIN_ACK" :
                    ((canErrorCount > 0) ? "INESTABLE" : "OK"));
  String rs485State = (totalWeatherErrors > 0) ? "FALLA" :
                      ((totalQueries == 0) ? "SIN_PRUEBA" : "OK");
  const String signature = tr1State + "|" + tr0State + "|" + canState + "|" +
                           rs485State + "|" + (traxUnknownCommands ? "TRAX_PENDIENTE" : "TRAX_OK");
  const unsigned long now = millis();

  if (signature == lastDiagnosticSignature &&
      (now - lastStatusTime) < DIAGNOSTIC_HEARTBEAT_MS) {
    return;
  }

  const bool changed = signature != lastDiagnosticSignature;
  lastDiagnosticSignature = signature;
  lastStatusTime = now;
  logLine(changed ? "---------------- CAMBIO DE ESTADO ----------------" :
                    "---------------- RESUMEN (60 s) ----------------");
  logLine("---------------- DIAGNOSTICO (" + String(millis() / 1000) + " s) ----------------");

  // TR1 es el camino aplicación <-> ESP real <-> TR1/TTL1 <-> simulador.
  if (tr1Frames == 0) {
    logLine("[TR1 SIN ACTIVIDAD] No llegó ninguna trama. Si la app está conectada: revisar TR1/TTL1, cruce TX/RX, GND común, 115200 8N1 y el puente BLE/UART2 del firmware.");
  } else if (lastTr1Qus07Time == 0) {
    logLine("[TR1 PARCIAL] RX=" + String(tr1Frames) + " TX=" + String(tr1Responses) +
            ", pero la app aún no pidió QUS07. Hay enlace serial, falta validar el reporte de mediciones.");
  } else {
    logLine("[TR1 OK] RX=" + String(tr1Frames) + " TX=" + String(tr1Responses) +
            "; último QUS07 hace " + elapsedText(lastTr1Qus07Time) + ". La app está consultando mediciones.");
  }

  // TR0 es el camino firmware real <-> TR0/TTL2 <-> simulador.
  if (tr0Frames == 0) {
    logLine("[TR0 SIN ACTIVIDAD] No hubo tramas del firmware real. Puede ser normal si no consulta TR0; si se espera tráfico, revisar TTL2, cruce TX/RX, GND, 115200 y que USB no esté interfiriendo con UART0.");
  } else if (lastStx08Time == 0) {
    logLine("[TR0 SIN STX08] RX=" + String(tr0Frames) + ". Hay enlace, pero no llegó el resumen CAN del firmware. Posibles causas: PGN no interpretado, tarea CAN detenida o firmware no publica STX08.");
  } else {
    logLine("[TR0 OK] RX=" + String(tr0Frames) + " TX=" + String(tr0Responses) +
            "; último STX08 hace " + elapsedText(lastStx08Time) + ": " + tx08Buffer);
  }

  if (!canReady) {
    logLine("[CAN FALLA] Controlador MCP2515 no disponible; no continuar con diagnóstico de aplicación hasta resolver inicialización.");
  } else if (canOkCount == 0) {
    logLine("[CAN SIN ACK] El MCP2515 inicializó pero ningún envío fue confirmado. Revisar cableado CAN y terminación.");
  } else if (canErrorCount > 0) {
    logLine("[CAN INESTABLE] OK=" + String(canOkCount) + " ERR=" + String(canErrorCount) + ". Hay pérdidas; guardar los códigos de error CAN.");
  } else {
    logLine("[CAN ELECTRICO OK] TX=" + String(canOkCount) + " sin errores. ACK presente; para probar interpretación completa se necesita STX08 por TR0.");
  }

  if (totalWeatherErrors > 0) {
    logLine("[RS485 FALLA] consultas=" + String(totalQueries) + " errores=" + String(totalWeatherErrors) + ". Guardar la primera consulta inválida mostrada arriba.");
  } else if (totalQueries == 0) {
    logLine("[RS485 SIN PRUEBA] No hubo consultas Modbus. Puede ser que clima no esté habilitado; si debería estarlo, revisar RX16/TX17, DE4 y la configuración del firmware.");
  } else {
    logLine("[RS485 OK] consultas=" + String(totalQueries) + " respuestas=" + String(totalResponses) +
            "; última consulta hace " + elapsedText(lastRs485QueryTime) + ".");
  }

  logLine("[MODO ESTRICTO] TX08 no se fuerza: la app sólo muestra CAN si la ESP real envía STX08 por TR0.");

  if (traxUnknownCommands > 0) {
    logLine("[TRAX ATENCION] Comandos no implementados acumulados=" + String(traxUnknownCommands) + ". Conservar esas tramas para ampliar el simulador.");
  }
  logLine("---------------------------------------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  TTL2.begin(TRAX_BAUD, SERIAL_8N1, TTL2_RX, TTL2_TX);
  TraxSerial.begin(TRAX_BAUD, SERIAL_8N1, TR1_RX, TR1_TX);
  RS485.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_RE_PIN, LOW);
  initCAN();

  logLine("");
  logLine("==========================================");
  logLine(" TRAX SIMULADOR COMPLETO CORREGIDO");
  logLine(" Bluetooth: TRAX SIMULATOR");
  logLine(" TR0/TTL2: UART0 RX32 TX33");
  logLine(" TR1/TTL1: UART2 RX25 TX26");
  logLine(" CAN: 250 kbps / MCP 8 MHz");
  logLine(" RS485: RX16 TX17 DE4 / 9600");
  logLine("==========================================");
}

void loop() {
  serviceRUSState();

  // Se atienden ambos canales del TRAX. Ninguno roba respuestas del otro.
  readTraxPort(TTL2, tr0RxBuffer, tr0DutDebugBuffer, "TR0/TTL2");
  readTraxPort(TraxSerial, tr1RxBuffer, tr1DutDebugBuffer, "TR1/TTL1");

  readWeather();
  serviceCAN();
  readCAN();
  showStatus();
  delay(1);
}
