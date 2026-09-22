#include <Arduino.h>
#include <BluetoothSerial.h>
#include <SPI.h>
#include <mcp2515.h>

#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif

#if DEVICE_ID != 1 && DEVICE_ID != 2
#error DEVICE_ID must be 1 or 2
#endif

// Existing board pin map.
static constexpr int TTL1_RX = 25;
static constexpr int TTL1_TX = 26;
static constexpr int TTL2_RX = 3;
static constexpr int TTL2_TX = 1;
static constexpr int RS485_RX = 16;
static constexpr int RS485_TX = 17;
static constexpr int RS485_DE_RE = 4;
static constexpr int PULSE_1 = 27;
static constexpr int PULSE_2 = 35;
static constexpr int CAN_CS = 5;
static constexpr int CAN_SCK = 18;
static constexpr int CAN_MISO = 19;
static constexpr int CAN_MOSI = 23;

static constexpr uint32_t LINK_BAUD = 115200;
static constexpr uint32_t RS485_BAUD = 9600;
static constexpr uint32_t PING_INTERVAL_MS = 2000;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 600;
static constexpr uint32_t STATUS_INTERVAL_MS = 5000;

HardwareSerial ttl1(2);
HardwareSerial rs485(1);
HardwareSerial& ttl2 = Serial;
BluetoothSerial bluetooth;
MCP2515 canController(CAN_CS);

struct LinkStats {
  const char* name;
  uint32_t sent = 0;
  uint32_t received = 0;
  uint32_t ok = 0;
  uint32_t errors = 0;
  uint32_t lastRx = 0;
  uint32_t pending = 0;
  uint32_t pendingSince = 0;

  explicit LinkStats(const char* linkName) : name(linkName) {}
};

LinkStats ttl1Stats{"TTL1"};
LinkStats ttl2Stats{"TTL2"};
LinkStats rs485Stats{"RS485"};

String ttl1Buffer;
String ttl2Buffer;
String rs485Buffer;
uint32_t rs485Bytes = 0;
uint32_t rs485Nulos = 0;
uint32_t nextSequence = 0;
uint32_t lastPingAt = 0;
uint32_t lastStatusAt = 0;
bool automaticTestsEnabled = true;
uint32_t canSent = 0;
uint32_t canReceived = 0;
uint32_t canOk = 0;
uint32_t canErrors = 0;
volatile uint32_t pulse1 = 0;
volatile uint32_t pulse2 = 0;
String bluetoothBuffer;

void IRAM_ATTR onPulse1() { ++pulse1; }
void IRAM_ATTR onPulse2() { ++pulse2; }

void report(const String& message) {
  if (bluetooth.hasClient()) {
    bluetooth.println(message);
  }
}

bool readLine(Stream& port, String& buffer, String& line) {
  while (port.available()) {
    const char c = static_cast<char>(port.read());
    if (&port == static_cast<Stream*>(&rs485)) {
      ++rs485Bytes;
      if (c == '\0') {
        ++rs485Nulos;
        continue;  // No agregar este byte al mensaje.
      }
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line = buffer;
      buffer = "";
      return line.length() > 0;
    }
    if (buffer.length() < 96) {
      buffer += c;
    } else {
      buffer = "";
    }
  }
  return false;
}

bool parseMessage(const String& message, const char* type, uint32_t& sender, uint32_t& sequence) {
  const String prefix = String(type) + ",";
  if (!message.startsWith(prefix)) {
    return false;
  }
  const int comma = message.indexOf(',', prefix.length());
  if (comma < 0) {
    return false;
  }
  sender = message.substring(prefix.length(), comma).toInt();
  sequence = message.substring(comma + 1).toInt();
  return sender >= 1 && sender <= 2;
}

void sendLine(HardwareSerial& port, LinkStats& stats, const String& message) {
  port.println(message);
  ++stats.sent;
  stats.pending = nextSequence;
  stats.pendingSince = millis();
}

void handleLinkMessage(HardwareSerial& port, LinkStats& stats, const String& message, bool halfDuplex) {
  uint32_t sender = 0;
  uint32_t sequence = 0;
  if (parseMessage(message, "PING", sender, sequence) && sender != DEVICE_ID) {
    ++stats.received;
    stats.lastRx = millis();
    if (halfDuplex) {
      digitalWrite(RS485_DE_RE, HIGH);
      delayMicroseconds(100);
    }
    port.print("PONG,");
    port.print(DEVICE_ID);
    port.print(',');
    port.println(sequence);
    port.flush();
    ++stats.sent;
    if (halfDuplex) {
      delayMicroseconds(100);
      digitalWrite(RS485_DE_RE, LOW);
    }
    return;
  }

  if (parseMessage(message, "PONG", sender, sequence) && sender != DEVICE_ID) {
    ++stats.received;
    stats.lastRx = millis();
    if (stats.pending == sequence) {
      ++stats.ok;
      stats.pending = 0;
    }
  }
}

void pollLink(HardwareSerial& port, String& buffer, LinkStats& stats, bool halfDuplex) {
  String line;
  while (readLine(port, buffer, line)) {
    handleLinkMessage(port, stats, line, halfDuplex);
  }
}

void sendRs485Ping() {
  if (DEVICE_ID != 1) {
    return;
  }
  const String message = "PING," + String(DEVICE_ID) + "," + String(nextSequence);
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(100);
  rs485.println(message);
  rs485.flush();
  delayMicroseconds(100);
  digitalWrite(RS485_DE_RE, LOW);
  ++rs485Stats.sent;
  rs485Stats.pending = nextSequence;
  rs485Stats.pendingSince = millis();
}

void sendCanPing() {
  struct can_frame frame{};
  frame.can_id = 0x100 + DEVICE_ID;
  frame.can_dlc = 4;
  frame.data[0] = DEVICE_ID;
  frame.data[1] = static_cast<uint8_t>(nextSequence & 0xFF);
  frame.data[2] = static_cast<uint8_t>((nextSequence >> 8) & 0xFF);
  frame.data[3] = 0xA5;
  if (canController.sendMessage(&frame) == MCP2515::ERROR_OK) {
    ++canSent;
  } else {
    ++canErrors;
  }
}

void pollCan() {
  struct can_frame frame{};
  while (canController.readMessage(&frame) == MCP2515::ERROR_OK) {
    if (frame.can_id != static_cast<uint32_t>(0x100 + (DEVICE_ID == 1 ? 2 : 1)) || frame.can_dlc < 4) {
      continue;
    }
    ++canReceived;
    if (frame.data[0] != DEVICE_ID && frame.data[3] == 0xA5) {
      ++canOk;
    } else {
      ++canErrors;
    }
  }
}

void checkTimeout(LinkStats& stats) {
  if (stats.pending != 0 && millis() - stats.pendingSince > RESPONSE_TIMEOUT_MS) {
    ++stats.errors;
    stats.pending = 0;
  }
}

String linkStatus(const LinkStats& stats) {
  return String(stats.name) + ": tx=" + stats.sent + " rx=" + stats.received +
         " ok=" + stats.ok + " err=" + stats.errors;
}

void printStatus() {
  const uint32_t p1 = pulse1;
  const uint32_t p2 = pulse2;
  report("--- HWTEST placa " + String(DEVICE_ID) + " ---");
  report("PRUEBAS AUTOMATICAS: " + String(automaticTestsEnabled ? "RUNNING" : "STOPPED"));
  report(linkStatus(ttl1Stats));
  report(linkStatus(ttl2Stats));
  report(linkStatus(rs485Stats));
  report("CAN: tx=" + String(canSent) + " rx=" + String(canReceived) +
         " ok=" + String(canOk) + " err=" + String(canErrors));
  report("PULSOS: pin27=" + String(p1) + " pin35=" + String(p2));
}

void printHelp() {
  report("HWTEST comandos: STATUS, RUN, STOP, HELP");
  report("Las pruebas comienzan automaticamente al arrancar");
  report("RUN reanuda; STOP detiene PING/PONG; STATUS solo consulta");
  report("UART0/TTL2 usa GPIO3/1: desconectar USB durante la prueba");
}

void processBluetoothCommand(const String& rawCommand) {
  String command = rawCommand;
  command.trim();
  command.toUpperCase();
  if (command == "STATUS") {
    printStatus();
  } else if (command == "HELP") {
    printHelp();
  } else if (command == "RUN") {
    automaticTestsEnabled = true;
    lastPingAt = 0;
    report("Pruebas automaticas activadas");
  } else if (command == "STOP") {
    automaticTestsEnabled = false;
    report("Pruebas automaticas detenidas");
  } else if (command.length() > 0) {
    report("Comando desconocido: " + command);
  }
}

void pollBluetooth() {
  while (bluetooth.available()) {
    const char c = static_cast<char>(bluetooth.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      processBluetoothCommand(bluetoothBuffer);
      bluetoothBuffer = "";
    } else if (bluetoothBuffer.length() < 64) {
      bluetoothBuffer += c;
    }
  }
}

void setup() {
  // UART0 is TTL2 in this test, so diagnostics are sent through Bluetooth.
  Serial.begin(LINK_BAUD, SERIAL_8N1, TTL2_RX, TTL2_TX);
  ttl1.begin(LINK_BAUD, SERIAL_8N1, TTL1_RX, TTL1_TX);
  rs485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);

  pinMode(PULSE_1, INPUT_PULLUP);
  pinMode(PULSE_2, INPUT);
  attachInterrupt(digitalPinToInterrupt(PULSE_1), onPulse1, RISING);
  attachInterrupt(digitalPinToInterrupt(PULSE_2), onPulse2, RISING);

  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
  canController.reset();
  canController.setBitrate(CAN_250KBPS, MCP_8MHZ);
  canController.setNormalMode();

  bluetooth.begin("HWTEST-ESP32-" + String(DEVICE_ID));
  delay(300);
  report("HWTEST listo. Placa " + String(DEVICE_ID));
  report("Conecte ambas placas y use STATUS o HELP");
}

void loop() {
  pollBluetooth();
  pollLink(ttl1, ttl1Buffer, ttl1Stats, false);
  pollLink(ttl2, ttl2Buffer, ttl2Stats, false);
  pollLink(rs485, rs485Buffer, rs485Stats, true);
  pollCan();

  const uint32_t now = millis();
  if (automaticTestsEnabled && now - lastPingAt >= PING_INTERVAL_MS) {
    ++nextSequence;
    const String ttl1Message = "PING," + String(DEVICE_ID) + "," + String(nextSequence);
    const String ttl2Message = ttl1Message;
    sendLine(ttl1, ttl1Stats, ttl1Message);
    sendLine(ttl2, ttl2Stats, ttl2Message);
    sendRs485Ping();
    sendCanPing();
    lastPingAt = now;
  }

  checkTimeout(ttl1Stats);
  checkTimeout(ttl2Stats);
  checkTimeout(rs485Stats);

  if (now - lastStatusAt >= STATUS_INTERVAL_MS) {
    printStatus();
    lastStatusAt = now;
  }
  delay(2);
}
