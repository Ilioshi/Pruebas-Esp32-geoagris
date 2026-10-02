#include <Arduino.h>
#include <BluetoothSerial.h>
#include <SPI.h>
#include <mcp2515.h>
#include <deque>

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
static constexpr uint32_t STATUS_INTERVAL_MS = 10000;
static constexpr uint32_t CAN_INFO_GAP_MS = 300;
static constexpr uint32_t CAN_INFO_START_BOARD_1_MS = 0;
static constexpr uint32_t CAN_INFO_START_BOARD_2_MS = 1700;
static constexpr uint32_t CAN_INFO_TIMEOUT_MS = 4000;
static constexpr uint32_t PEER_STALE_MS = 15000;
static constexpr uint32_t REPORT_SETTLE_MS = 3500;
static constexpr size_t RS485_SAMPLE_MAX_BYTES = 128;
static constexpr size_t LINK_TRACE_MAX_BYTES = 1024;
static constexpr size_t CAN_TRACE_MAX_FRAMES = 32;
static constexpr size_t TRACE_CHUNK_BYTES = 80;
static constexpr uint8_t CHANNEL_COUNT = 4;
static constexpr uint8_t COMPARISON_KINDS = CHANNEL_COUNT + 1;  // prueba + 4 informes.
static const char* const CHANNEL_NAMES[CHANNEL_COUNT] = {"TTL1", "TTL2", "RS485", "CAN"};

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
  uint32_t rawBytes = 0;
  uint32_t completeLines = 0;
  uint32_t invalidLines = 0;
  uint32_t pingSent = 0;
  uint32_t pingReceived = 0;
  uint32_t pongSent = 0;
  uint32_t pongReceived = 0;
  uint32_t lastPingSequence = 0;
  uint32_t okFromPreviousWindow = 0;
  uint32_t errorsFromPreviousWindow = 0;
  uint32_t lastTimeoutSequence = 0;
  bool lastTimeoutWasPrevious = false;
  String lastRaw;

  explicit LinkStats(const char* linkName) : name(linkName) {}
};

LinkStats ttl1Stats{"TTL1"};
LinkStats ttl2Stats{"TTL2"};
LinkStats rs485Stats{"RS485"};

String ttl1Buffer;
String ttl2Buffer;
String rs485Buffer;
uint32_t rs485Nulos = 0;
String rs485WindowSampleHex;
uint32_t rs485WindowSampleBytes = 0;
String rs485ReportSampleHex;
uint32_t rs485ReportSampleBytes = 0;
struct PeerRs485Sample {
  uint8_t reportId = 0;
  uint32_t bytes = 0;
  String hex;
  uint32_t receivedAt = 0;
};
PeerRs485Sample peerRs485Sample;
struct Rs485ReportTrace {
  uint8_t reportId = 0;
  bool started = false;
  bool txReported = false;
  bool complete = false;
  uint32_t startedAt = 0;
  uint32_t txReportedAt = 0;
  uint32_t txBytes = 0;
  uint32_t rxBytes = 0;
  String expectedHex;
  String receivedHex;
};
Rs485ReportTrace rs485ReportTrace;
uint32_t infoBytesSent[CHANNEL_COUNT]{};
uint32_t infoBytesReceived[CHANNEL_COUNT]{};
uint32_t previousInfoBytesSent[CHANNEL_COUNT]{};
uint32_t previousInfoBytesReceived[CHANNEL_COUNT]{};
uint32_t nextSequence = 0;
uint32_t lastPingAt = 0;
uint32_t lastStatusAt = 0;
uint32_t lastWindowCapturedAt = 0;
bool automaticTestsEnabled = true;
uint32_t canSent = 0;
uint32_t canReceived = 0;
uint32_t canOk = 0;
uint32_t canErrors = 0;
uint32_t canRxOverflowEvents = 0;
uint32_t canOverflowRx0 = 0;
uint32_t canOverflowRx1 = 0;
uint32_t previousCanOverflowRx0 = 0;
uint32_t previousCanOverflowRx1 = 0;
uint8_t canOverflowRx0Window = 0;
uint8_t canOverflowRx1Window = 0;
volatile uint32_t pulse1 = 0;
volatile uint32_t pulse2 = 0;
uint32_t localPulse1 = 0;
uint32_t localPulse2 = 0;
uint32_t previousPulse1 = 0;
uint32_t previousPulse2 = 0;
uint32_t pulseRatePrevious1 = 0;
uint32_t pulseRatePrevious2 = 0;
uint32_t pulseRateSampleAt = 0;
bool pulseRateReady = false;
uint32_t localPulseRate1 = 0;  // Decimas de pulso por segundo.
uint32_t localPulseRate2 = 0;
uint32_t peerPulse1 = 0;
uint32_t peerPulse2 = 0;
uint32_t peerPulseRate1 = 0;
uint32_t peerPulseRate2 = 0;
uint8_t peerPulseReportId = 0;
uint32_t peerPulseReceivedAt = 0;
String bluetoothBuffer;

struct ChannelWindow {
  uint8_t tx = 0;
  uint8_t rx = 0;
  uint8_t ok = 0;
  uint8_t errors = 0;
  uint8_t raw = 0;
  uint8_t invalid = 0;
};

struct FlowWindow {
  uint8_t pingTx = 0;
  uint8_t pingRx = 0;
  uint8_t pongTx = 0;
  uint8_t pongRx = 0;
  uint16_t infoTx = 0;
  uint16_t infoRx = 0;
  uint32_t lastPingSequence = 0;
  uint8_t okFromPreviousWindow = 0;
  uint8_t errorsFromPreviousWindow = 0;
  uint8_t ownPingPending = 0;
  uint32_t lastTimeoutSequence = 0;
  uint8_t lastTimeoutWasPrevious = 0;
};

ChannelWindow makeWindow(uint8_t tx, uint8_t rx, uint8_t ok, uint8_t errors,
                         uint8_t raw, uint8_t invalid) {
  ChannelWindow window;
  window.tx = tx;
  window.rx = rx;
  window.ok = ok;
  window.errors = errors;
  window.raw = raw;
  window.invalid = invalid;
  return window;
}

struct PeerCopy {
  ChannelWindow window;
  uint8_t reportId = 0;
  uint32_t receivedAt = 0;
};

struct FrameComparison {
  String expected;
  String received;
  uint8_t verdict = 0;  // 1=coincide, 2=difiere, 3=formato invalido, 4=sin referencia exacta.
  uint8_t reportId = 0;
  bool seen = false;
};

struct PeerFrameComparison {
  FrameComparison frame;
  uint8_t reportId = 0;
  uint32_t receivedAt = 0;
};

struct RawTrace {
  uint32_t count = 0;  // Bytes UART o tramas CAN.
  String hex;          // Ventana acotada; el detalle indica cualquier recorte.
};

struct PeerRawTrace {
  RawTrace trace;
  uint8_t reportId = 0;
  uint32_t receivedAt = 0;
};

RawTrace rxTraceNow[CHANNEL_COUNT];
RawTrace txTraceNow[CHANNEL_COUNT];
RawTrace rxTraceWindow[CHANNEL_COUNT];
RawTrace txTraceWindow[CHANNEL_COUNT];
PeerRawTrace peerRxTrace[CHANNEL_COUNT];
PeerRawTrace peerTxTrace[CHANNEL_COUNT];
PeerRawTrace pendingRxTrace[CHANNEL_COUNT];
PeerRawTrace pendingTxTrace[CHANNEL_COUNT];
struct ReportReceipt {
  uint8_t reportId = 0;
  uint8_t mask = 0;
  uint32_t receivedAt = 0;
};
ReportReceipt peerReceipts[CHANNEL_COUNT];
uint32_t receiptDue = 0;
std::deque<String> diagnosticQueue;
std::deque<String> bluetoothQueue;
uint32_t diagnosticNextAt = 0;
uint32_t bluetoothNextAt = 0;
uint32_t diagnosticRejected = 0;
uint32_t diagnosticDropped = 0;

ChannelWindow localWindow[CHANNEL_COUNT];
FlowWindow localFlow[CHANNEL_COUNT];
struct FlowCopy {
  FlowWindow window;
  uint8_t reportId = 0;
};
FlowCopy peerFlow[CHANNEL_COUNT];
uint32_t previousFlowCounters[CHANNEL_COUNT][6]{};
FrameComparison liveComparison[CHANNEL_COUNT][COMPARISON_KINDS];
FrameComparison localComparison[CHANNEL_COUNT][COMPARISON_KINDS];
PeerFrameComparison peerComparison[CHANNEL_COUNT][COMPARISON_KINDS];
PeerCopy peerCopies[CHANNEL_COUNT][CHANNEL_COUNT];  // [via][canal medido]
uint32_t previousCounters[CHANNEL_COUNT][6]{};
uint8_t localReportId = 0;
uint8_t latestPeerReportId = 0;
uint32_t latestPeerAt = 0;
uint32_t latestPeerFirstAt = 0;
uint8_t rs485ScheduledFor = 0;
uint32_t rs485ReportDue = 0;
bool rs485AwaitingReady = false;
uint8_t rs485ReadyReportId = 0;
uint8_t rs485AnnouncedReportId = 0;
bool rs485PingDeferred = false;
ChannelWindow canInfoWindows[CHANNEL_COUNT];
uint8_t canInfoReportId = 0;
uint8_t canInfoNextChannel = 0;
uint32_t canInfoQueuedAt = 0;
uint32_t canInfoNextSendAt = 0;
uint32_t canInfoDropped = 0;
bool canInfoPending = false;
uint32_t statusPrintDue = 0;

void IRAM_ATTR onPulse1() { ++pulse1; }
void IRAM_ATTR onPulse2() { ++pulse2; }

void report(const String& message) {
  if (bluetooth.hasClient()) {
    bluetoothQueue.push_back(message);
  }
}

void processBluetoothOutput() {
  if (!bluetooth.hasClient()) {
    bluetoothQueue.clear();
    return;
  }
  if (bluetoothQueue.empty() ||
      static_cast<int32_t>(millis() - bluetoothNextAt) < 0) return;
  bluetooth.println(bluetoothQueue.front());
  bluetoothQueue.pop_front();
  bluetoothNextAt = millis() + 10;
}

uint16_t diagnosticCrc(const String& text) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < text.length(); ++i) {
    crc ^= static_cast<uint16_t>(static_cast<uint8_t>(text[i])) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void sendDiagnostic(const String& text, bool urgent = false) {
  char checksum[6];
  snprintf(checksum, sizeof(checksum), "*%04X", diagnosticCrc(text));
  if (diagnosticQueue.size() >= 512) {
    ++diagnosticDropped;
    return;
  }
  if (urgent) diagnosticQueue.push_front(text + checksum);
  else diagnosticQueue.push_back(text + checksum);
}

void processDiagnosticOutput() {
  if (diagnosticQueue.empty() ||
      static_cast<int32_t>(millis() - diagnosticNextAt) < 0) return;
  const String& text = diagnosticQueue.front();
  if (ttl1.availableForWrite() < static_cast<int>(text.length() + 130)) return;
  ttl1.println(text);
  diagnosticQueue.pop_front();
  diagnosticNextAt = millis() + 15;
}

void sendPulseSample() {
  if (DEVICE_ID != 2 || !pulseRateReady) return;
  // Sustituir una muestra aun en cola para no entregar tasas viejas despues.
  for (auto it = diagnosticQueue.begin(); it != diagnosticQueue.end();) {
    if (it->startsWith("PULSE,")) it = diagnosticQueue.erase(it);
    else ++it;
  }
  // Contadores de la ultima ventana cerrada de 10 s y tasa del ultimo segundo.
  sendDiagnostic("PULSE,2," + String(localReportId) + "," +
                 String(localPulse1) + "," + String(localPulse2) + "," +
                 String(localPulseRate1) + "," + String(localPulseRate2), true);
}

void samplePulseRate() {
  if (DEVICE_ID != 2) return;
  const uint32_t now = millis();
  const uint32_t elapsed = now - pulseRateSampleAt;
  if (elapsed < 1000) return;
  noInterrupts();
  const uint32_t count1 = pulse1;
  const uint32_t count2 = pulse2;
  interrupts();
  // Normalizar por el tiempo real evita error por el pequeno retraso del loop.
  localPulseRate1 = static_cast<uint32_t>(
    static_cast<uint64_t>(count1 - pulseRatePrevious1) * 10000 / elapsed);
  localPulseRate2 = static_cast<uint32_t>(
    static_cast<uint64_t>(count2 - pulseRatePrevious2) * 10000 / elapsed);
  pulseRatePrevious1 = count1;
  pulseRatePrevious2 = count2;
  pulseRateSampleAt = now;
  pulseRateReady = true;
  sendPulseSample();
}

bool isDiagnostic(const String& text) {
  return text.startsWith("FLOW,") || text.startsWith("CMP,") ||
         text.startsWith("PULSE,") || text.startsWith("TRACE,") ||
         text.startsWith("RRX,") || text.startsWith("RBEGIN,") ||
         text.startsWith("RREADY,") || text.startsWith("RTX,") ||
         text.startsWith("RCPT,");
}

bool verifyDiagnostic(String& text) {
  const int marker = text.lastIndexOf('*');
  if (marker < 0 || text.length() - marker != 5) return false;
  const String checksum = text.substring(marker + 1);
  for (uint8_t i = 0; i < 4; ++i)
    if (!isDigit(checksum[i]) && (checksum[i] < 'A' || checksum[i] > 'F'))
      return false;
  const String body = text.substring(0, marker);
  if (diagnosticCrc(body) != strtoul(checksum.c_str(), nullptr, 16)) return false;
  text = body;
  return true;
}

void rememberRawByte(LinkStats& stats, uint8_t value) {
  const char hex[] = "0123456789ABCDEF";
  String shown;
  if (value == '\r') shown = "<CR>";
  else if (value == '\n') shown = "<LF>";
  else if (value >= 32 && value <= 126) shown = String(static_cast<char>(value));
  else {
    shown = "<";
    shown += hex[value >> 4];
    shown += hex[value & 0x0F];
    shown += ">";
  }
  stats.lastRaw += shown;
  if (stats.lastRaw.length() > 96) {
    stats.lastRaw.remove(0, stats.lastRaw.length() - 96);
  }
}

String toHex(const String& input) {
  const char digits[] = "0123456789ABCDEF";
  String result;
  for (size_t i = 0; i < input.length() && i < 96; ++i) {
    const uint8_t byte = static_cast<uint8_t>(input[i]);
    result += digits[byte >> 4];
    result += digits[byte & 0x0F];
  }
  return result;
}

String fromHex(const String& input) {
  String result;
  if (input.length() % 2 != 0) return result;
  for (size_t i = 0; i + 1 < input.length(); i += 2) {
    const char highChar = input[i];
    const char lowChar = input[i + 1];
    const int high = highChar >= '0' && highChar <= '9' ? highChar - '0' :
                     highChar >= 'A' && highChar <= 'F' ? highChar - 'A' + 10 : -1;
    const int low = lowChar >= '0' && lowChar <= '9' ? lowChar - '0' :
                    lowChar >= 'A' && lowChar <= 'F' ? lowChar - 'A' + 10 : -1;
    if (high < 0 || low < 0) return "";
    result += static_cast<char>((high << 4) | low);
  }
  return result;
}

void rememberComparison(uint8_t channel, uint8_t kind, const String& expected,
                        const String& received, uint8_t verdict,
                        uint8_t reportId = 0) {
  FrameComparison& frame = liveComparison[channel][kind];
  // Conservar la ultima trama fallida de la ventana, aunque luego llegue una correcta.
  if (frame.seen && frame.reportId == reportId &&
      frame.verdict != 1 && frame.verdict != 4 &&
      (verdict == 1 || verdict == 4)) return;
  frame.expected = expected.substring(0, 96);
  frame.received = received.substring(0, 96);
  frame.verdict = verdict;
  frame.reportId = reportId;
  frame.seen = true;
}

String showHex(const String& input) {
  String result;
  for (size_t i = 0; i + 1 < input.length(); i += 2) {
    const int high = isDigit(input[i]) ? input[i] - '0' : input[i] - 'A' + 10;
    const int low = isDigit(input[i + 1]) ? input[i + 1] - '0' : input[i + 1] - 'A' + 10;
    const uint8_t byte = static_cast<uint8_t>((high << 4) | low);
    if (byte >= 32 && byte <= 126) result += static_cast<char>(byte);
    else if (byte == '\r') result += "<CR>";
    else if (byte == '\n') result += "<LF>";
    else {
      result += '<';
      result += input.substring(i, i + 2);
      result += '>';
    }
  }
  return result;
}

void appendHexByte(String& hexText, uint8_t value, size_t maxBytes) {
  if (hexText.length() >= maxBytes * 2) return;
  const char digits[] = "0123456789ABCDEF";
  hexText += digits[value >> 4];
  hexText += digits[value & 0x0F];
}

void traceByte(RawTrace& trace, uint8_t value) {
  ++trace.count;
  if (trace.hex.length() >= LINK_TRACE_MAX_BYTES * 2)
    trace.hex.remove(0, 2);  // Conservar los bytes mas recientes.
  appendHexByte(trace.hex, value, LINK_TRACE_MAX_BYTES);
}

void traceText(RawTrace& trace, const String& value) {
  for (size_t i = 0; i < value.length(); ++i)
    traceByte(trace, static_cast<uint8_t>(value[i]));
}

void traceLine(RawTrace& trace, const String& value) {
  traceText(trace, value);
  traceText(trace, "\r\n");
}

void traceCanFrame(RawTrace& trace, const struct can_frame& frame) {
  ++trace.count;
  if (trace.hex.length() >= CAN_TRACE_MAX_FRAMES * 26)
    trace.hex.remove(0, 26);  // 4 bytes ID, 1 DLC y 8 de datos.
  for (uint8_t i = 0; i < 4; ++i)
    appendHexByte(trace.hex, static_cast<uint8_t>(frame.can_id >> (i * 8)),
                  CAN_TRACE_MAX_FRAMES * 13);
  appendHexByte(trace.hex, frame.can_dlc, CAN_TRACE_MAX_FRAMES * 13);
  for (uint8_t i = 0; i < 8; ++i)
    appendHexByte(trace.hex, i < frame.can_dlc ? frame.data[i] : 0,
                  CAN_TRACE_MAX_FRAMES * 13);
}

String toHexComplete(const String& input) {
  String result;
  result.reserve(input.length() * 2);
  for (size_t i = 0; i < input.length(); ++i)
    appendHexByte(result, static_cast<uint8_t>(input[i]), input.length());
  return result;
}

bool validHexText(const String& input) {
  if (input.length() % 2 != 0) return false;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (!isDigit(c) && (c < 'A' || c > 'F')) return false;
  }
  return true;
}

bool readLine(Stream& port, String& buffer, String& line, LinkStats& stats) {
  while (port.available()) {
    const char c = static_cast<char>(port.read());
    ++stats.rawBytes;
    rememberRawByte(stats, static_cast<uint8_t>(c));
    if (&port == static_cast<Stream*>(&ttl1))
      traceByte(rxTraceNow[0], static_cast<uint8_t>(c));
    else if (&port == static_cast<Stream*>(&ttl2))
      traceByte(rxTraceNow[1], static_cast<uint8_t>(c));
    else if (&port == static_cast<Stream*>(&rs485))
      traceByte(rxTraceNow[2], static_cast<uint8_t>(c));
    if (&port == static_cast<Stream*>(&rs485)) {
      ++rs485WindowSampleBytes;
      appendHexByte(rs485WindowSampleHex, static_cast<uint8_t>(c),
                    RS485_SAMPLE_MAX_BYTES);
      if (DEVICE_ID == 1 && rs485ReportTrace.started &&
          !rs485ReportTrace.complete) {
        ++rs485ReportTrace.rxBytes;
        appendHexByte(rs485ReportTrace.receivedHex, static_cast<uint8_t>(c),
                      RS485_SAMPLE_MAX_BYTES);
      }
    }
    if (&port == static_cast<Stream*>(&rs485) && c == '\0') {
      ++rs485Nulos;
      if (buffer.length() > 0)
        buffer += "<00>";  // Dentro de una trama: conservarlo para invalidarla.
      continue;  // Antes de una trama: no impedir un PING/PONG posterior valido.
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line = buffer;
      buffer = "";
      if (line.length() > 0) {
        ++stats.completeLines;
        return true;
      }
      continue;
    }
    if (buffer.length() < 512) {
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
  const String senderText = message.substring(prefix.length(), comma);
  const String sequenceText = message.substring(comma + 1);
  if ((senderText != "1" && senderText != "2") || sequenceText.length() == 0) return false;
  for (size_t i = 0; i < sequenceText.length(); ++i) {
    if (sequenceText[i] < '0' || sequenceText[i] > '9') return false;
  }
  sender = senderText.toInt();
  sequence = sequenceText.toInt();
  return sequence != 0;
}

uint8_t cappedDelta(uint32_t current, uint32_t& previous) {
  const uint32_t difference = current - previous;
  previous = current;
  return difference > 255 ? 255 : static_cast<uint8_t>(difference);
}

uint16_t cappedDelta16(uint32_t current, uint32_t& previous) {
  const uint32_t difference = current - previous;
  previous = current;
  return difference > 65535 ? 65535 : static_cast<uint16_t>(difference);
}

void captureWindow(uint8_t channel, uint32_t tx, uint32_t rx, uint32_t ok,
                   uint32_t errors, uint32_t raw, uint32_t invalid) {
  ChannelWindow& window = localWindow[channel];
  window.tx = cappedDelta(tx, previousCounters[channel][0]);
  window.rx = cappedDelta(rx, previousCounters[channel][1]);
  window.ok = cappedDelta(ok, previousCounters[channel][2]);
  window.errors = cappedDelta(errors, previousCounters[channel][3]);
  window.raw = cappedDelta(raw, previousCounters[channel][4]);
  window.invalid = cappedDelta(invalid, previousCounters[channel][5]);
}

void captureAllWindows() {
  ++localReportId;
  if (localReportId == 0) ++localReportId;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    rxTraceWindow[channel] = rxTraceNow[channel];
    txTraceWindow[channel] = txTraceNow[channel];
    rxTraceNow[channel] = RawTrace{};
    txTraceNow[channel] = RawTrace{};
  }
  rs485ReportSampleHex = rs485WindowSampleHex;
  rs485ReportSampleBytes = rs485WindowSampleBytes;
  rs485WindowSampleHex = "";
  rs485WindowSampleBytes = 0;
  captureWindow(0, ttl1Stats.sent, ttl1Stats.received, ttl1Stats.ok,
                ttl1Stats.errors, ttl1Stats.rawBytes, ttl1Stats.invalidLines);
  captureWindow(1, ttl2Stats.sent, ttl2Stats.received, ttl2Stats.ok,
                ttl2Stats.errors, ttl2Stats.rawBytes, ttl2Stats.invalidLines);
  captureWindow(2, rs485Stats.sent, rs485Stats.received, rs485Stats.ok,
                rs485Stats.errors, rs485Stats.rawBytes, rs485Nulos);
  captureWindow(3, canSent, canReceived, canOk, canErrors, canReceived,
                canRxOverflowEvents);
  canOverflowRx0Window = cappedDelta(canOverflowRx0, previousCanOverflowRx0);
  canOverflowRx1Window = cappedDelta(canOverflowRx1, previousCanOverflowRx1);
  if (DEVICE_ID == 2) {
    noInterrupts();
    const uint32_t count1 = pulse1;
    const uint32_t count2 = pulse2;
    interrupts();
    localPulse1 = count1 - previousPulse1;
    localPulse2 = count2 - previousPulse2;
    previousPulse1 = count1;
    previousPulse2 = count2;
  }
  LinkStats* serialStats[3] = {&ttl1Stats, &ttl2Stats, &rs485Stats};
  for (uint8_t channel = 0; channel < 3; ++channel) {
    LinkStats& stats = *serialStats[channel];
    FlowWindow& flow = localFlow[channel];
    flow.pingTx = cappedDelta(stats.pingSent, previousFlowCounters[channel][0]);
    flow.pingRx = cappedDelta(stats.pingReceived, previousFlowCounters[channel][1]);
    flow.pongTx = cappedDelta(stats.pongSent, previousFlowCounters[channel][2]);
    flow.pongRx = cappedDelta(stats.pongReceived, previousFlowCounters[channel][3]);
    flow.lastPingSequence = stats.lastPingSequence;
    flow.okFromPreviousWindow = cappedDelta(stats.okFromPreviousWindow,
                                            previousFlowCounters[channel][4]);
    flow.errorsFromPreviousWindow = cappedDelta(stats.errorsFromPreviousWindow,
                                                previousFlowCounters[channel][5]);
    flow.ownPingPending = stats.pending != 0 &&
                          static_cast<int32_t>(stats.pendingSince -
                                               lastWindowCapturedAt) >= 0 ? 1 : 0;
    flow.lastTimeoutSequence = localWindow[channel].errors > 0 ?
                               stats.lastTimeoutSequence : 0;
    flow.lastTimeoutWasPrevious = stats.lastTimeoutWasPrevious ? 1 : 0;
  }
  localFlow[3].pingTx = cappedDelta(canSent, previousFlowCounters[3][0]);
  localFlow[3].pingRx = cappedDelta(canReceived, previousFlowCounters[3][1]);
  localFlow[3].lastPingSequence = nextSequence;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    localFlow[channel].infoTx = cappedDelta16(infoBytesSent[channel],
                                               previousInfoBytesSent[channel]);
    localFlow[channel].infoRx = cappedDelta16(infoBytesReceived[channel],
                                               previousInfoBytesReceived[channel]);
    for (uint8_t kind = 0; kind < COMPARISON_KINDS; ++kind) {
      localComparison[channel][kind] = liveComparison[channel][kind];
      liveComparison[channel][kind] = FrameComparison{};
    }
  }
  lastWindowCapturedAt = millis();
}

String infoLine(uint8_t channel) {
  const ChannelWindow& w = localWindow[channel];
  return "INFO," + String(DEVICE_ID) + "," + String(localReportId) + "," +
         String(channel) + "," +
         String(w.tx) + "," + String(w.rx) + "," + String(w.ok) + "," +
         String(w.errors) + "," + String(w.raw) + "," + String(w.invalid);
}

String flowLine(uint8_t channel) {
  const FlowWindow& flow = localFlow[channel];
  return "FLOW," + String(DEVICE_ID) + "," + String(localReportId) + "," +
         String(channel) + "," + String(flow.pingTx) + "," +
         String(flow.pingRx) + "," + String(flow.pongTx) + "," +
         String(flow.pongRx) + "," + String(flow.lastPingSequence) + "," +
         String(flow.infoTx) + "," + String(flow.infoRx) + "," +
         String(flow.okFromPreviousWindow) + "," +
         String(flow.errorsFromPreviousWindow) + "," +
         String(flow.ownPingPending) + "," +
         String(flow.lastTimeoutSequence) + "," +
         String(flow.lastTimeoutWasPrevious);
}

void sendInfoSerial(HardwareSerial& port, bool halfDuplex) {
  const uint8_t via = &port == &ttl1 ? 0 : (&port == &ttl2 ? 1 : 2);
  if (halfDuplex) {
    digitalWrite(RS485_DE_RE, HIGH);
    delayMicroseconds(100);
  }
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const String line = infoLine(channel);
    infoBytesSent[via] += port.println(line);
    traceLine(txTraceNow[via], line);
  }
  // El desglose y la ultima comparacion de cada canal viajan por TTL1.
  if (&port == &ttl1) {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel)
      sendDiagnostic(flowLine(channel));
    sendPulseSample();
    if (DEVICE_ID == 2)
      sendDiagnostic("RRX,2," + String(localReportId) + "," +
                   String(rs485ReportSampleBytes) + "," +
                   (rs485ReportSampleHex.length() ? rs485ReportSampleHex : "-"));
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      for (uint8_t kind = 0; kind < 1; ++kind) {
        const FrameComparison& frame = localComparison[channel][kind];
        if (!frame.seen) {
          sendDiagnostic("CMP," + String(DEVICE_ID) + "," + String(localReportId) +
                         "," + String(channel) + ",0,0,-,-");
          continue;
        }
        sendDiagnostic("CMP," + String(DEVICE_ID) + "," + String(localReportId) +
                     "," + String(channel) + "," + String(kind) + "," +
                     String(frame.verdict) + "," + toHex(frame.expected) +
                     "," + toHex(frame.received));
      }
    }
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      const RawTrace* traces[] = {&txTraceWindow[channel],
                                  &rxTraceWindow[channel]};
      for (uint8_t direction = 0; direction < 2; ++direction) {
        const RawTrace& trace = *traces[direction];
        size_t offset = 0;
        do {
          const String chunk = trace.hex.substring(offset * 2,
            (offset + TRACE_CHUNK_BYTES) * 2);
          const bool last = (offset * 2 + chunk.length()) == trace.hex.length();
          sendDiagnostic("TRACE," + String(DEVICE_ID) + "," +
                         String(localReportId) + "," + String(channel) + "," +
                         (direction == 0 ? "T" : "R") + "," +
                         String(trace.count) + "," + String(offset) + "," +
                         (last ? "1" : "0") + "," +
                         (chunk.length() ? chunk : "-"));
          offset += chunk.length() / 2;
          if (last) break;
        } while (offset * 2 < trace.hex.length());
      }
    }
  }
  if (halfDuplex) {
    port.flush();
    delayMicroseconds(100);
    digitalWrite(RS485_DE_RE, LOW);
  }
}

void sendInfoCan() {
  if (canInfoPending) ++canInfoDropped;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel)
    canInfoWindows[channel] = localWindow[channel];
  canInfoReportId = localReportId;
  canInfoNextChannel = 0;
  canInfoQueuedAt = millis();
  // Dejar terminar primero la rafaga de placa 1 y evitar que ambas llenen
  // a la vez los dos buffers RX del MCP2515 del equipo opuesto.
  canInfoNextSendAt = canInfoQueuedAt +
    (DEVICE_ID == 1 ? CAN_INFO_START_BOARD_1_MS : CAN_INFO_START_BOARD_2_MS);
  canInfoPending = true;
}

void processCanInfoQueue() {
  if (!canInfoPending) return;
  const uint32_t now = millis();
  if (now - canInfoQueuedAt > CAN_INFO_TIMEOUT_MS) {
    ++canInfoDropped;
    canInfoPending = false;
    return;
  }
  if (static_cast<int32_t>(now - canInfoNextSendAt) < 0) return;

  const uint8_t channel = canInfoNextChannel;
  const ChannelWindow& w = canInfoWindows[channel];
  struct can_frame frame{};
  frame.can_id = (DEVICE_ID == 1 ? 0x300 : 0x400) + canInfoReportId;
  frame.can_dlc = 8;
  frame.data[0] = 0xD2;
  frame.data[1] = channel;
  frame.data[2] = w.tx;
  frame.data[3] = w.rx;
  frame.data[4] = w.ok;
  frame.data[5] = w.errors;
  frame.data[6] = w.raw;
  frame.data[7] = w.invalid;
  if (canController.sendMessage(&frame) != MCP2515::ERROR_OK) return;
  traceCanFrame(txTraceNow[3], frame);
  infoBytesSent[3] += frame.can_dlc;
  ++canInfoNextChannel;
  if (canInfoNextChannel == CHANNEL_COUNT) {
    canInfoPending = false;
  } else {
    canInfoNextSendAt = now + CAN_INFO_GAP_MS;
  }
}

void storePeerCopy(uint8_t via, uint8_t channel, uint8_t reportId,
                   const ChannelWindow& window) {
  const uint32_t now = millis();
  PeerCopy& copy = peerCopies[via][channel];
  copy.window = window;
  copy.reportId = reportId;
  copy.receivedAt = now;
  if (latestPeerReportId == 0 || now - latestPeerAt > PEER_STALE_MS ||
      static_cast<int8_t>(reportId - latestPeerReportId) > 0) {
    latestPeerReportId = reportId;
    latestPeerFirstAt = now;
    receiptDue = now + REPORT_SETTLE_MS;
  }
  if (reportId == latestPeerReportId) latestPeerAt = now;
  // La placa 2 transmite su resumen por RS485 aunque el pedido solo haya
  // llegado por otro enlace; el retardo evita solaparlo con el envio de placa 1.
  if (DEVICE_ID == 2 && channel == CHANNEL_COUNT - 1 &&
      rs485ScheduledFor != reportId) {
    if (localReportId == 0) {
      captureAllWindows();
      sendInfoSerial(ttl1, false);
      sendInfoSerial(ttl2, false);
      sendInfoCan();
    }
    rs485ScheduledFor = reportId;
    rs485ReportDue = now + 500;
  }
}

bool receiveInfo(const String& message, uint8_t via, const String& expected) {
  if (!message.startsWith("INFO,")) return false;
  unsigned long sender = 0, reportId = 0, channel = 0, tx, rx, ok, errors, raw, invalid;
  char extra;
  const int parsed = sscanf(message.c_str(), "INFO,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu%c",
                            &sender, &reportId, &channel, &tx, &rx, &ok, &errors, &raw,
                            &invalid, &extra);
  const uint8_t kind = channel < CHANNEL_COUNT ?
                       static_cast<uint8_t>(1 + channel) : 1;
  if (parsed != 9 || sender != (DEVICE_ID == 1 ? 2UL : 1UL) ||
      reportId == 0 || reportId > 255 ||
      channel >= CHANNEL_COUNT || tx > 255 || rx > 255 || ok > 255 ||
      errors > 255 || raw > 255 || invalid > 255) {
    rememberComparison(via, kind,
                       expected.length() ? expected :
                       "INFO,placa,informe,canal,tx,rx,ok,err,bytes,invalid",
                       message, 3,
                       reportId > 0 && reportId <= 255 ? reportId : 0);
    return true;
  }
  rememberComparison(via, kind, expected, message,
                     !expected.length() ? 4 : (expected != message ? 2 : 1),
                     reportId);
  infoBytesReceived[via] += message.length() + 2;  // println agrega CR+LF.
  storePeerCopy(via, channel, static_cast<uint8_t>(reportId),
                makeWindow(static_cast<uint8_t>(tx), static_cast<uint8_t>(rx),
                           static_cast<uint8_t>(ok), static_cast<uint8_t>(errors),
                           static_cast<uint8_t>(raw), static_cast<uint8_t>(invalid)));
  return true;
}

bool receiveComparison(const String& message) {
  if (!message.startsWith("CMP,")) return false;
  unsigned long sender, reportId, channel, kind, verdict;
  char expectedHex[193]{};
  char receivedHex[193]{};
  char extra;
  const int parsed = sscanf(message.c_str(),
                            "CMP,%lu,%lu,%lu,%lu,%lu,%192[0-9A-F-],%192[0-9A-F-]%c",
                            &sender, &reportId, &channel, &kind, &verdict,
                            expectedHex, receivedHex, &extra);
  const size_t expectedLength = strlen(expectedHex);
  const size_t receivedLength = strlen(receivedHex);
  if (parsed == 7 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
      reportId > 0 && reportId <= 255 && channel < CHANNEL_COUNT &&
      kind == 0 && verdict == 0 && String(expectedHex) == "-" &&
      String(receivedHex) == "-") {
    PeerFrameComparison& copy = peerComparison[channel][0];
    copy = PeerFrameComparison{};
    copy.reportId = static_cast<uint8_t>(reportId);
    copy.receivedAt = millis();
    return true;
  }
  if (parsed == 7 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
      reportId > 0 && reportId <= 255 && channel < CHANNEL_COUNT &&
      kind < COMPARISON_KINDS &&
      verdict >= 1 && verdict <= 4 && expectedLength > 0 &&
      receivedLength > 0 && expectedLength % 2 == 0 &&
      receivedLength % 2 == 0 && validHexText(expectedHex) &&
      validHexText(receivedHex)) {
    const String expected = fromHex(expectedHex);
    const String received = fromHex(receivedHex);
    if (kind == 0 &&
        !(channel == 3 ? expected.startsWith("ID=0x") :
          expected.startsWith("PING,") || expected.startsWith("PONG,")))
      return true;  // Una comparacion etiquetada con el canal equivocado no es fiable.
    if (kind == 0 && verdict == 1) {
      uint32_t source = 0, sequence = 0;
      const bool validReceived = channel == 3 ? expected == received :
        ((parseMessage(received, "PING", source, sequence) ||
          parseMessage(received, "PONG", source, sequence)) &&
         source == DEVICE_ID &&
         (!received.startsWith("PONG,") || expected == received));
      if (!validReceived) {
        ++diagnosticRejected;
        return true;
      }
    }
    PeerFrameComparison& copy = peerComparison[channel][kind];
    copy.reportId = static_cast<uint8_t>(reportId);
    copy.receivedAt = millis();
    copy.frame.expected = expected;
    copy.frame.received = received;
    copy.frame.verdict = static_cast<uint8_t>(verdict);
    copy.frame.seen = true;
  }
  return true;
}

bool receiveFlow(const String& message) {
  if (!message.startsWith("FLOW,")) return false;
  unsigned long sender, reportId, channel, pingTx, pingRx, pongTx, pongRx;
  unsigned long sequence, infoTx, infoRx, okPrevious, errorsPrevious;
  unsigned long ownPending, timeoutSequence, timeoutPrevious;
  char extra;
  const int parsed = sscanf(message.c_str(),
                            "FLOW,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu%c",
                            &sender, &reportId, &channel, &pingTx, &pingRx,
                            &pongTx, &pongRx, &sequence, &infoTx, &infoRx,
                            &okPrevious, &errorsPrevious, &ownPending,
                            &timeoutSequence, &timeoutPrevious, &extra);
  if (parsed == 15 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
      reportId > 0 && reportId <= 255 && channel < CHANNEL_COUNT &&
      pingTx <= 255 && pingRx <= 255 && pongTx <= 255 && pongRx <= 255 &&
      infoTx <= 65535 && infoRx <= 65535 && okPrevious <= 255 &&
      errorsPrevious <= 255 && ownPending <= 1 && timeoutPrevious <= 1) {
    FlowCopy& copy = peerFlow[channel];
    copy.reportId = static_cast<uint8_t>(reportId);
    copy.window.pingTx = static_cast<uint8_t>(pingTx);
    copy.window.pingRx = static_cast<uint8_t>(pingRx);
    copy.window.pongTx = static_cast<uint8_t>(pongTx);
    copy.window.pongRx = static_cast<uint8_t>(pongRx);
    copy.window.infoTx = static_cast<uint16_t>(infoTx);
    copy.window.infoRx = static_cast<uint16_t>(infoRx);
    copy.window.lastPingSequence = sequence;
    copy.window.okFromPreviousWindow = static_cast<uint8_t>(okPrevious);
    copy.window.errorsFromPreviousWindow = static_cast<uint8_t>(errorsPrevious);
    copy.window.ownPingPending = static_cast<uint8_t>(ownPending);
    copy.window.lastTimeoutSequence = timeoutSequence;
    copy.window.lastTimeoutWasPrevious = static_cast<uint8_t>(timeoutPrevious);
  }
  return true;
}

bool receivePulse(const String& message) {
  if (!message.startsWith("PULSE,")) return false;
  unsigned long sender, reportId, count27, count35, rate27, rate35;
  char extra;
  const int parsed = sscanf(message.c_str(), "PULSE,%lu,%lu,%lu,%lu,%lu,%lu%c",
                            &sender, &reportId, &count27, &count35,
                            &rate27, &rate35, &extra);
  if (DEVICE_ID == 1 && parsed == 6 && sender == 2 &&
      reportId <= 255) {  // Cero: ya hay tasa de 1 s, aun no cerro el primer bloque.
    peerPulseReportId = static_cast<uint8_t>(reportId);
    peerPulse1 = count27;
    peerPulse2 = count35;
    peerPulseRate1 = rate27;
    peerPulseRate2 = rate35;
    peerPulseReceivedAt = millis();
  }
  return true;
}

bool receiveRawTrace(const String& message) {
  if (!message.startsWith("TRACE,")) return false;
  unsigned long sender = 0, reportId = 0, channel = 0, count = 0;
  unsigned long offset = 0, last = 0;
  char direction = 0, sample[TRACE_CHUNK_BYTES * 2 + 2]{};
  char extra;
  const int parsed = sscanf(message.c_str(),
                            "TRACE,%lu,%lu,%lu,%c,%lu,%lu,%lu,%160s%c",
                            &sender, &reportId, &channel, &direction,
                            &count, &offset, &last, sample, &extra);
  const String hex = sample;
  if (parsed == 8 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
      reportId > 0 && reportId <= 255 && channel < CHANNEL_COUNT &&
      last <= 1 && offset <= LINK_TRACE_MAX_BYTES &&
      (direction == 'T' || direction == 'R') &&
      (hex == "-" || (hex.length() <= TRACE_CHUNK_BYTES * 2 &&
                       validHexText(hex)))) {
    PeerRawTrace& pending = direction == 'T' ? pendingTxTrace[channel] :
                                                  pendingRxTrace[channel];
    if (offset == 0) {
      pending = PeerRawTrace{};
      pending.reportId = static_cast<uint8_t>(reportId);
      pending.trace.count = count;
    }
    const String chunk = hex == "-" ? "" : hex;
    const size_t limit = channel == 3 ? CAN_TRACE_MAX_FRAMES * 13 :
                                        LINK_TRACE_MAX_BYTES;
    if (pending.reportId != reportId || pending.trace.count != count ||
        pending.trace.hex.length() / 2 != offset ||
        offset + chunk.length() / 2 > limit) {
      ++diagnosticRejected;
      return true;
    }
    pending.trace.hex += chunk;
    if (last) {
      const size_t bytes = pending.trace.hex.length() / 2;
      const size_t wanted = channel == 3 ?
        (count > CAN_TRACE_MAX_FRAMES ? CAN_TRACE_MAX_FRAMES : count) * 13 :
        (count > LINK_TRACE_MAX_BYTES ? LINK_TRACE_MAX_BYTES : count);
      if (bytes != wanted) {
        ++diagnosticRejected;
        return true;
      }
      pending.receivedAt = millis();
      (direction == 'T' ? peerTxTrace[channel] : peerRxTrace[channel]) = pending;
      pending = PeerRawTrace{};
    }
  } else {
    ++diagnosticRejected;
  }
  return true;
}

bool receiveReceipt(const String& message) {
  if (!message.startsWith("RCPT,")) return false;
  unsigned long sender = 0, channel = 0, reportId = 0, mask = 0;
  char extra;
  if (sscanf(message.c_str(), "RCPT,%lu,%lu,%lu,%lu%c", &sender,
             &channel, &reportId, &mask, &extra) == 4 &&
      sender == (DEVICE_ID == 1 ? 2UL : 1UL) && channel < CHANNEL_COUNT &&
      reportId > 0 && reportId <= 255 && mask <= 15) {
    peerReceipts[channel].reportId = reportId;
    peerReceipts[channel].mask = mask;
    peerReceipts[channel].receivedAt = millis();
  }
  return true;
}

uint8_t receivedInfoMask(uint8_t via, uint8_t reportId) {
  uint8_t mask = 0;
  if (reportId == 0) return mask;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const PeerCopy& copy = peerCopies[via][channel];
    if (copy.reportId == reportId &&
        millis() - copy.receivedAt <= PEER_STALE_MS) mask |= 1 << channel;
  }
  return mask;
}

void processReceipts() {
  if (receiptDue == 0 || static_cast<int32_t>(millis() - receiptDue) < 0) return;
  receiptDue = 0;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel)
    sendDiagnostic("RCPT," + String(DEVICE_ID) + "," + String(channel) + "," +
                   String(latestPeerReportId) + "," +
                   String(receivedInfoMask(channel, latestPeerReportId)), true);
}

bool receiveRs485Trace(const String& message) {
  if (message.startsWith("RRX,")) {
    unsigned long sender = 0, reportId = 0, bytes = 0;
    char hex[RS485_SAMPLE_MAX_BYTES * 2 + 2]{};
    char extra;
    const int parsed = sscanf(message.c_str(), "RRX,%lu,%lu,%lu,%257s%c",
                              &sender, &reportId, &bytes, hex, &extra);
    const String sample = hex;
    if (DEVICE_ID == 1 && parsed == 4 && sender == 2 &&
        reportId > 0 && reportId <= 255 &&
        (sample == "-" || (sample.length() <= RS485_SAMPLE_MAX_BYTES * 2 &&
         validHexText(sample)))) {
      peerRs485Sample.reportId = static_cast<uint8_t>(reportId);
      peerRs485Sample.bytes = bytes;
      peerRs485Sample.hex = sample == "-" ? "" : sample;
      peerRs485Sample.receivedAt = millis();
    }
    return true;
  }
  if (message.startsWith("RBEGIN,")) {
    unsigned long sender = 0, reportId = 0;
    char extra;
    if (DEVICE_ID == 1 &&
        sscanf(message.c_str(), "RBEGIN,%lu,%lu%c", &sender, &reportId,
               &extra) == 2 && sender == 2 && reportId > 0 && reportId <= 255) {
      rs485ReportTrace = Rs485ReportTrace{};
      rs485ReportTrace.reportId = static_cast<uint8_t>(reportId);
      rs485ReportTrace.started = true;
      rs485ReportTrace.startedAt = millis();
      sendDiagnostic("RREADY,1," + String(reportId), true);
    }
    return true;
  }
  if (message.startsWith("RREADY,")) {
    unsigned long sender = 0, reportId = 0;
    char extra;
    if (DEVICE_ID == 2 &&
        sscanf(message.c_str(), "RREADY,%lu,%lu%c", &sender, &reportId,
               &extra) == 2 && sender == 1 &&
        reportId == rs485AnnouncedReportId)
      rs485ReadyReportId = static_cast<uint8_t>(reportId);
    return true;
  }
  if (message.startsWith("RTX,")) {
    unsigned long sender = 0, reportId = 0, bytes = 0;
    char hex[401]{};
    char extra;
    const int parsed = sscanf(message.c_str(), "RTX,%lu,%lu,%lu,%400[0-9A-F]%c",
                              &sender, &reportId, &bytes, hex, &extra);
    const String expected = hex;
    if (DEVICE_ID == 1 && parsed == 4 && sender == 2 &&
        reportId > 0 && reportId <= 255 && expected.length() > 0 &&
        expected.length() % 2 == 0) {
      if (rs485ReportTrace.reportId != reportId) {
        rs485ReportTrace = Rs485ReportTrace{};
        rs485ReportTrace.reportId = static_cast<uint8_t>(reportId);
      }
      rs485ReportTrace.txReported = true;
      rs485ReportTrace.txReportedAt = millis();
      rs485ReportTrace.txBytes = bytes;
      rs485ReportTrace.expectedHex = expected;
    }
    return true;
  }
  return false;
}

void sendLine(HardwareSerial& port, LinkStats& stats, const String& message) {
  port.println(message);
  traceLine(txTraceNow[&stats == &ttl1Stats ? 0 : 1], message);
  ++stats.sent;
  ++stats.pingSent;
  stats.lastPingSequence = nextSequence;
  stats.pending = nextSequence;
  stats.pendingSince = millis();
}

String expectedInfoLine(uint8_t via, const String& message) {
  unsigned long sender = 0, reportId = 0, channel = 0;
  if (sscanf(message.c_str(), "INFO,%lu,%lu,%lu", &sender, &reportId, &channel) == 3 &&
      sender == (DEVICE_ID == 1 ? 2UL : 1UL) && reportId > 0 &&
      reportId <= 255 && channel < CHANNEL_COUNT) {
    const PeerCopy* first = nullptr;
    for (uint8_t referenceVia = 0; referenceVia < CHANNEL_COUNT; ++referenceVia) {
      if (referenceVia == via) continue;
      const PeerCopy& reference = peerCopies[referenceVia][channel];
      if (reference.reportId != reportId ||
          millis() - reference.receivedAt > PEER_STALE_MS) continue;
      if (!first || millis() - reference.receivedAt >
                    millis() - first->receivedAt) first = &reference;
    }
    if (first) {
      const ChannelWindow& w = first->window;
      return "INFO," + String(sender) + "," + String(reportId) + "," +
             String(channel) + "," + String(w.tx) + "," + String(w.rx) +
             "," + String(w.ok) + "," + String(w.errors) + "," +
             String(w.raw) + "," + String(w.invalid);
    }
  }
  return "";  // Primera copia: el contenido aun no puede contrastarse.
}

bool handleLinkMessage(HardwareSerial& port, LinkStats& stats, String message, bool halfDuplex) {
  const uint8_t via = &stats == &ttl1Stats ? 0 : (&stats == &ttl2Stats ? 1 : 2);
  if (isDiagnostic(message) && (via != 0 || !verifyDiagnostic(message))) {
    ++diagnosticRejected;
    return true;
  }
  const String infoExpected = message.startsWith("INFO,") ?
                              expectedInfoLine(via, message) : "";
  if (receiveInfo(message, via, infoExpected)) return true;
  if (receiveComparison(message)) return true;
  if (receiveFlow(message)) return true;
  if (receivePulse(message)) return true;
  if (receiveRawTrace(message)) return true;
  if (receiveReceipt(message)) return true;
  if (receiveRs485Trace(message)) return true;
  uint32_t sender = 0;
  uint32_t sequence = 0;
  if (parseMessage(message, "PING", sender, sequence) && sender != DEVICE_ID) {
    rememberComparison(via, 0,
                       "PING," + String(DEVICE_ID == 1 ? 2 : 1) + "," + String(sequence),
                       message, 1);
    ++stats.received;
    ++stats.pingReceived;
    stats.lastRx = millis();
    if (halfDuplex) {
      digitalWrite(RS485_DE_RE, HIGH);
      delayMicroseconds(100);
    }
    port.print("PONG,");
    port.print(DEVICE_ID);
    port.print(',');
    port.println(sequence);
    if (halfDuplex) port.flush();
    traceLine(txTraceNow[via], "PONG," + String(DEVICE_ID) + "," +
              String(sequence));
    ++stats.sent;
    ++stats.pongSent;
    if (halfDuplex) {
      delayMicroseconds(100);
      digitalWrite(RS485_DE_RE, LOW);
    }
    return true;
  }

  if (parseMessage(message, "PONG", sender, sequence) && sender != DEVICE_ID) {
    const uint32_t expectedSequence = stats.pending;
    rememberComparison(via, 0,
                       "PONG," + String(DEVICE_ID == 1 ? 2 : 1) + "," +
                       (expectedSequence ? String(expectedSequence) : String("<sin PING pendiente>")),
                       message,
                       expectedSequence != 0 && expectedSequence == sequence ? 1 : 2);
    ++stats.received;
    ++stats.pongReceived;
    stats.lastRx = millis();
    if (stats.pending != 0 && stats.pending == sequence) {
      ++stats.ok;
      if (static_cast<int32_t>(stats.pendingSince - lastWindowCapturedAt) < 0)
        ++stats.okFromPreviousWindow;
      stats.pending = 0;
    }
    return true;
  }
  ++stats.invalidLines;
  const String expected = stats.pending != 0
    ? "PONG," + String(DEVICE_ID == 1 ? 2 : 1) + "," + String(stats.pending)
    : "PING," + String(DEVICE_ID == 1 ? 2 : 1) + ",secuencia";
  rememberComparison(via, 0, expected, message, 3);
  return false;
}

void pollLink(HardwareSerial& port, String& buffer, LinkStats& stats, bool halfDuplex) {
  String line;
  while (readLine(port, buffer, line, stats)) {
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
  traceLine(txTraceNow[2], message);
  rs485.flush();
  delayMicroseconds(100);
  digitalWrite(RS485_DE_RE, LOW);
  ++rs485Stats.sent;
  ++rs485Stats.pingSent;
  rs485Stats.lastPingSequence = nextSequence;
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
    traceCanFrame(txTraceNow[3], frame);
  } else {
    ++canErrors;
  }
}

String canFrameText(const struct can_frame& frame) {
  String result = "ID=0x" + String(frame.can_id, HEX) +
                  " DLC=" + String(frame.can_dlc) + " DATA=";
  const char digits[] = "0123456789ABCDEF";
  const uint8_t length = frame.can_dlc <= 8 ? frame.can_dlc : 8;
  for (uint8_t i = 0; i < length; ++i) {
    if (i) result += ' ';
    result += digits[frame.data[i] >> 4];
    result += digits[frame.data[i] & 0x0F];
  }
  return result;
}

void pollCan() {
  struct can_frame frame{};
  while (canController.readMessage(&frame) == MCP2515::ERROR_OK) {
    traceCanFrame(rxTraceNow[3], frame);
    const uint32_t peerInfoBase = DEVICE_ID == 1 ? 0x400 : 0x300;
    if (frame.can_id > peerInfoBase && frame.can_id <= peerInfoBase + 255) {
      const uint8_t reportId = static_cast<uint8_t>(frame.can_id - peerInfoBase);
      const bool valid = frame.can_dlc == 8 && frame.data[0] == 0xD2 &&
                         frame.data[1] < CHANNEL_COUNT;
      String expected;
      uint8_t verdict = valid ? 4 : 3;
      if (valid) {
        const uint8_t channel = frame.data[1];
        const PeerCopy* first = nullptr;
        for (uint8_t referenceVia = 0; referenceVia < CHANNEL_COUNT; ++referenceVia) {
          if (referenceVia == 3) continue;
          const PeerCopy& reference = peerCopies[referenceVia][channel];
          if (reference.reportId != reportId ||
              millis() - reference.receivedAt > PEER_STALE_MS) continue;
          if (!first || millis() - reference.receivedAt >
                        millis() - first->receivedAt) first = &reference;
        }
        if (first) {
          const PeerCopy& reference = *first;
          struct can_frame referenceFrame{};
          referenceFrame.can_id = frame.can_id;
          referenceFrame.can_dlc = 8;
          referenceFrame.data[0] = 0xD2;
          referenceFrame.data[1] = channel;
          referenceFrame.data[2] = reference.window.tx;
          referenceFrame.data[3] = reference.window.rx;
          referenceFrame.data[4] = reference.window.ok;
          referenceFrame.data[5] = reference.window.errors;
          referenceFrame.data[6] = reference.window.raw;
          referenceFrame.data[7] = reference.window.invalid;
          expected = canFrameText(referenceFrame);
          verdict = expected == canFrameText(frame) ? 1 : 2;
        }
        rememberComparison(3, 1 + channel, expected, canFrameText(frame),
                           verdict, reportId);
        infoBytesReceived[3] += frame.can_dlc;
        storePeerCopy(3, channel, reportId,
                      makeWindow(frame.data[2], frame.data[3], frame.data[4],
                                 frame.data[5], frame.data[6], frame.data[7]));
      } else {
        rememberComparison(3, 1, "CAN INFO ID/DLC=8 DATA=D2 canal + 6 datos",
                           canFrameText(frame), verdict, reportId);
      }
      continue;
    }
    if (frame.can_id != static_cast<uint32_t>(0x100 + (DEVICE_ID == 1 ? 2 : 1))) {
      continue;
    }
    ++canReceived;
    const bool valid = frame.can_dlc == 4 &&
                       frame.data[0] == (DEVICE_ID == 1 ? 2 : 1) &&
                       frame.data[3] == 0xA5;
    struct can_frame expectedFrame{};
    expectedFrame.can_id = 0x100 + (DEVICE_ID == 1 ? 2 : 1);
    expectedFrame.can_dlc = 4;
    expectedFrame.data[0] = DEVICE_ID == 1 ? 2 : 1;
    expectedFrame.data[1] = frame.data[1];
    expectedFrame.data[2] = frame.data[2];
    expectedFrame.data[3] = 0xA5;
    rememberComparison(3, 0, canFrameText(expectedFrame),
                       canFrameText(frame), valid ? 1 : 3);
    if (valid) {
      ++canOk;
    } else {
      ++canErrors;
    }
  }
  const uint8_t flags = canController.getErrorFlags();
  if (flags & 0xC0) {  // MCP2515: RX1OVR | RX0OVR.
    ++canRxOverflowEvents;
    if (flags & 0x40) ++canOverflowRx0;
    if (flags & 0x80) ++canOverflowRx1;
    canController.clearRXnOVRFlags();
  }
}

void checkTimeout(LinkStats& stats) {
  if (stats.pending != 0 && millis() - stats.pendingSince > RESPONSE_TIMEOUT_MS) {
    ++stats.errors;
    stats.lastTimeoutSequence = stats.pending;
    stats.lastTimeoutWasPrevious =
      static_cast<int32_t>(stats.pendingSince - lastWindowCapturedAt) < 0;
    if (stats.lastTimeoutWasPrevious) ++stats.errorsFromPreviousWindow;
    stats.pending = 0;
  }
}

String windowState(uint8_t channel, uint8_t device, const ChannelWindow& w) {
  if (channel == 2 && device == 2) {
    if (w.rx > 0) return "OK (respondio pedidos)";
    if (w.raw == 0) return "FALLA: no recibe pedidos";
  } else if (w.ok > 0 && w.errors > 0) {
    return "INTERMITENTE: " + String(w.errors) + " PING vencidos sin PONG";
  } else if (channel == 3 && w.ok > 0 && w.invalid > 0) {
    return "OK en tramas recibidas; buffer RX lleno";
  } else if (w.ok > 0) {
    return "OK";
  }
  if (w.tx == 0 && w.rx == 0 && w.raw == 0) return "SIN PRUEBA";
  if (channel == 2 && w.rx == 0 && w.raw > 0 && w.raw == w.invalid)
    return device == 1 ? "FALLA: sin PONG (solo NUL en 10 s)" :
                         "FALLA: sin PING (solo NUL en 10 s)";
  if (w.raw == 0) return "FALLA: sin datos en RX";
  if (w.rx == 0) return "FALLA: bytes sin PING/PONG valido";
  return channel == 1 && device == 1 ? "FALLA: sin PONG de placa 2" :
                                      "FALLA: RX OK; TX sin respuesta";
}

void reportComparison(const String& name, const FrameComparison* frame) {
  if (!frame) {
    report("  " + name + ": " +
           (latestPeerReportId && millis() - latestPeerFirstAt < REPORT_SETTLE_MS ?
            "comparacion auxiliar en camino" :
            "sin comparacion auxiliar reciente (TTL1)"));
    return;
  }
  if (!frame->seen) {
    report("  " + name + ": ninguna trama completa en estos 10 s");
    return;
  }
  const String verdict = frame->verdict == 1 ? "CORRECTA" :
                         frame->verdict == 2 ? "NO COINCIDE" :
                         frame->verdict == 3 ? "FORMATO INVALIDO" :
                         "PRIMERA COPIA (sin otra para comparar)";
  report("  " + name + ": " + verdict);
  if (frame->expected.length())
    report("    esperado: " + showHex(toHex(frame->expected)));
  report("    recibido: " + showHex(toHex(frame->received)));
}

void reportWindow(uint8_t channel, uint8_t device, const ChannelWindow& w,
                  const FlowWindow* flow, const FrameComparison* testFrame) {
  report("Estado: " + windowState(channel, device, w));
  if (channel == 3) {
    report("  CAN TX=" + String(w.tx) + " RX=" + String(w.rx) +
           " correctas=" + String(w.ok) + " errores=" + String(w.errors));
    if (w.invalid > 0)
      report("  Buffer RX lleno " + String(w.invalid) + " vez/veces");
  } else if (flow) {
    const uint8_t ownOk = w.ok >= flow->okFromPreviousWindow ?
                          w.ok - flow->okFromPreviousWindow : 0;
    const uint8_t ownTimeouts = w.errors >= flow->errorsFromPreviousWindow ?
                                w.errors - flow->errorsFromPreviousWindow : 0;
    if (channel == 2 && device == 2) {
      report("  PING de placa 1 recibidos=" + String(flow->pingRx) +
             "; PONG enviados=" + String(flow->pongTx));
    } else {
      report("  PING enviados=" + String(flow->pingTx) +
             "; PONG confirmados=" + String(ownOk) +
             "; sin PONG=" + String(ownTimeouts) +
             "; pendientes=" + String(flow->ownPingPending));
      if (flow->okFromPreviousWindow > 0 ||
          flow->errorsFromPreviousWindow > 0)
        report("  Del bloque anterior: PONG=" +
               String(flow->okFromPreviousWindow) + "; sin PONG=" +
               String(flow->errorsFromPreviousWindow));
      if (w.errors > 0)
        report("  Ultima espera vencida: PONG," +
               String(device == 1 ? 2 : 1) + "," +
               String(flow->lastTimeoutSequence) +
               (flow->lastTimeoutWasPrevious ?
                " (bloque anterior)" : ""));
      if (channel < 2)
        report("  PING del otro recibidos=" + String(flow->pingRx) +
               "; PONG enviados=" + String(flow->pongTx));
    }
  } else {
    report("  TX=" + String(w.tx) + " RX validos=" + String(w.rx) +
           " PONG confirmados=" + String(w.ok) +
           " (sin desglose FLOW reciente)");
  }
  if (testFrame && testFrame->seen &&
      testFrame->verdict != 1 && testFrame->verdict != 4)
    reportComparison("Trama de prueba no valida", testFrame);
}

String briefState(uint8_t channel, uint8_t device, const ChannelWindow& w) {
  const String state = windowState(channel, device, w);
  if (state.startsWith("OK")) return state.startsWith("OK en") ?
                                  "OK; buffer CAN RX lleno (ver DATOS)" : "OK";
  if (state.startsWith("INTERMITENTE")) return state;
  if (state == "SIN PRUEBA") return "SIN PRUEBA";
  if (channel == 2 && device == 2 && w.raw == 0) return "FALLA: sin PING de placa 1";
  if (w.raw == 0) return "FALLA: RX sin datos";
  if (w.rx == 0) return "FALLA: RX datos incorrectos";
  return "FALLA: falta PONG correcto";
}

const PeerCopy* currentPeerCopy(uint8_t channel);

String localBriefState(uint8_t channel) {
  const ChannelWindow& local = localWindow[channel];
  const PeerCopy* peer = currentPeerCopy(channel);
  if (channel < 2 && peer && (local.errors > 0 || peer->window.errors > 0)) {
    const ChannelWindow& board2 = DEVICE_ID == 2 ? local : peer->window;
    const ChannelWindow& board1 = DEVICE_ID == 1 ? local : peer->window;
    const FlowWindow* board1Flow = DEVICE_ID == 1 ? &localFlow[channel] :
      (peerFlow[channel].reportId == latestPeerReportId ? &peerFlow[channel].window : nullptr);
    const FlowWindow* board2Flow = DEVICE_ID == 2 ? &localFlow[channel] :
      (peerFlow[channel].reportId == latestPeerReportId ? &peerFlow[channel].window : nullptr);
    const bool rx1Ok = board1Flow ? board1Flow->pingRx > 0 : board1.rx > 0;
    const bool rx2Ok = board2Flow ? board2Flow->pingRx > 0 : board2.rx > 0;
    if (DEVICE_ID == 1) {
      if (rx1Ok && !rx2Ok) return "placa 2 RX falla (no PING); TX OK";
      if (!rx1Ok && rx2Ok) return "RX sin respuesta; TX OK";
      if (!rx1Ok && !rx2Ok) return "RX sin respuesta; TX sin respuesta";
      return "RX OK; TX/PONG sin confirmar";
    }
    if (rx2Ok && !rx1Ok) return "RX OK; TX sin respuesta";
    if (!rx2Ok && rx1Ok) return "placa 2 RX falla (no PING); TX OK";
    if (!rx2Ok && !rx1Ok) return "RX sin respuesta; TX sin respuesta";
    return "RX OK; TX/PONG sin confirmar";
  }
  if (channel == 2 && peer &&
      (DEVICE_ID == 1 ? local.errors : peer->window.errors) > 0) {
    const ChannelWindow& board2 = DEVICE_ID == 2 ? local : peer->window;
    const ChannelWindow& board1 = DEVICE_ID == 1 ? local : peer->window;
    if (board2.raw == 0) return "placa 2 RX sin PING; sin PONG";
    if (board2.rx == 0) return "placa 2 RX datos no validos; sin PONG";
    if (board1.ok > 0)
      return "INTERMITENTE: " + String(board1.errors) +
             " PING de placa 1 sin PONG; otros OK";
    return "placa 2 RX OK; ningun PONG confirmado en placa 1";
  }
  return briefState(channel, DEVICE_ID, local);
}

void reportBoard2Pulses() {
  if (DEVICE_ID == 2 && pulseRateReady) {
    report("Pulsos placa 2 (1 s): GPIO27=" +
           String(localPulseRate1 / 10) + "." +
           String(localPulseRate1 % 10) + " p/s; GPIO35=" +
           String(localPulseRate2 / 10) + "." +
           String(localPulseRate2 % 10) + " p/s");
  } else if (DEVICE_ID == 1 && peerPulseReceivedAt != 0 &&
             millis() - peerPulseReceivedAt <= 2500) {
    report("Pulsos placa 2 (1 s): GPIO27=" +
           String(peerPulseRate1 / 10) + "." +
           String(peerPulseRate1 % 10) + " p/s; GPIO35=" +
           String(peerPulseRate2 / 10) + "." +
           String(peerPulseRate2 % 10) + " p/s");
  } else {
    report("Pulsos placa 2 (1 s): sin dato reciente");
  }
  if (DEVICE_ID == 2 && localReportId != 0)
    report("  Contador (10 s): GPIO27=" + String(localPulse1) +
           " GPIO35=" + String(localPulse2));
  else if (DEVICE_ID == 1 && peerPulseReportId != 0 &&
           peerPulseReportId == latestPeerReportId &&
           millis() - latestPeerAt <= PEER_STALE_MS &&
           millis() - peerPulseReceivedAt <= PEER_STALE_MS)
    report("  Contador (10 s): GPIO27=" + String(peerPulse1) +
           " GPIO35=" + String(peerPulse2));
  else
    report("  Contador (10 s): sin ventana reciente completa");
}

void printBriefStatus() {
  report("------------------------------");
  report("PLACA " + String(DEVICE_ID) + " - ULTIMOS 10 SEGUNDOS");
  if (!automaticTestsEnabled) {
    report("Pruebas detenidas. Escribe RUN para reanudar.");
  } else {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel)
      report(String(CHANNEL_NAMES[channel]) + ": " +
             localBriefState(channel));
  }
  reportBoard2Pulses();
  report("------------------------------");
}

const PeerCopy* currentPeerCopy(uint8_t channel) {
  if (latestPeerReportId == 0 || millis() - latestPeerAt > PEER_STALE_MS) return nullptr;
  for (uint8_t via = 0; via < CHANNEL_COUNT; ++via) {
    const PeerCopy& copy = peerCopies[via][channel];
    if (copy.reportId == latestPeerReportId &&
        millis() - copy.receivedAt <= PEER_STALE_MS) return &copy;
  }
  return nullptr;
}

bool sameWindow(const ChannelWindow& a, const ChannelWindow& b) {
  return a.tx == b.tx && a.rx == b.rx && a.ok == b.ok &&
         a.errors == b.errors && a.raw == b.raw && a.invalid == b.invalid;
}

const PeerCopy* firstPeerCopy(uint8_t channel) {
  if (latestPeerReportId == 0 || millis() - latestPeerAt > PEER_STALE_MS)
    return nullptr;
  const PeerCopy* first = nullptr;
  for (uint8_t via = 0; via < CHANNEL_COUNT; ++via) {
    const PeerCopy& copy = peerCopies[via][channel];
    if (copy.reportId != latestPeerReportId ||
        millis() - copy.receivedAt > PEER_STALE_MS) continue;
    if (!first || millis() - copy.receivedAt > millis() - first->receivedAt)
      first = &copy;
  }
  return first;
}

String peerInfoText(uint8_t channel, const PeerCopy& copy) {
  const ChannelWindow& w = copy.window;
  return "INFO," + String(DEVICE_ID == 1 ? 2 : 1) + "," +
         String(copy.reportId) + "," + String(channel) + "," +
         String(w.tx) + "," + String(w.rx) + "," + String(w.ok) + "," +
         String(w.errors) + "," + String(w.raw) + "," + String(w.invalid);
}

void reportPeerCopyMatrix() {
  if (latestPeerReportId == 0 || millis() - latestPeerAt > PEER_STALE_MS) return;
  report("======== COPIAS INFO PLACA " + String(DEVICE_ID == 1 ? 2 : 1) +
         " (informe " + String(latestPeerReportId) + ") ========");
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const PeerCopy* first = firstPeerCopy(channel);
    if (first)
      report("  Referencia " + String(CHANNEL_NAMES[channel]) + ": " +
             peerInfoText(channel, *first));
  }
  for (uint8_t via = 0; via < CHANNEL_COUNT; ++via) {
    String summary = "  via " + String(CHANNEL_NAMES[via]) + ": ";
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      const PeerCopy& copy = peerCopies[via][channel];
      if (channel) summary += " | ";
      summary += CHANNEL_NAMES[channel];
      summary += '=';
      if (copy.reportId == latestPeerReportId &&
          millis() - copy.receivedAt <= PEER_STALE_MS) {
        const PeerCopy* first = firstPeerCopy(channel);
        summary += &copy == first ? "REF" :
                   first && sameWindow(copy.window, first->window) ?
                   "IGUAL" : "DIFERENTE";
        continue;
      }
      const FrameComparison& live = liveComparison[via][1 + channel];
      const FrameComparison& saved = localComparison[via][1 + channel];
      const FrameComparison* malformed = live.seen && live.verdict == 3 &&
        live.reportId == latestPeerReportId ? &live :
        saved.seen && saved.verdict == 3 &&
        saved.reportId == latestPeerReportId ? &saved : nullptr;
      if (malformed) {
        summary += "INVALIDA";
      } else {
        summary += millis() - latestPeerFirstAt < REPORT_SETTLE_MS ?
                   "EN CAMINO" : "NO LLEGA";
      }
    }
    report(summary);
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      const PeerCopy& copy = peerCopies[via][channel];
      const PeerCopy* first = firstPeerCopy(channel);
      const String label = "    " + String(CHANNEL_NAMES[via]) +
                           " / " + CHANNEL_NAMES[channel] + ": ";
      if (first && copy.reportId == latestPeerReportId &&
          millis() - copy.receivedAt <= PEER_STALE_MS &&
          !sameWindow(copy.window, first->window)) {
        report(label + "DIFERENTE");
        report("      esperado: " + peerInfoText(channel, *first));
        report("      recibido: " + peerInfoText(channel, copy));
        continue;
      }
      const FrameComparison& live = liveComparison[via][1 + channel];
      const FrameComparison& saved = localComparison[via][1 + channel];
      const FrameComparison* malformed = live.seen && live.verdict == 3 &&
        live.reportId == latestPeerReportId ? &live :
        saved.seen && saved.verdict == 3 &&
        saved.reportId == latestPeerReportId ? &saved : nullptr;
      if (malformed && (copy.reportId != latestPeerReportId ||
                        millis() - copy.receivedAt > PEER_STALE_MS)) {
        report(label + "TRAMA INVALIDA");
        report("      esperado: " + malformed->expected);
        report("      recibido: " + showHex(toHex(malformed->received)));
      }
    }
  }
}

uint8_t validRs485Reports(uint8_t reportId) {
  if (reportId == 0) return 0;
  uint8_t validReports = 0;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const PeerCopy& copy = peerCopies[2][channel];
    if (copy.reportId == reportId &&
        millis() - copy.receivedAt <= PEER_STALE_MS) ++validReports;
  }
  return validReports;
}

void reportRs485Exchange() {
  if (!rs485ReportTrace.txReported ||
      millis() - rs485ReportTrace.txReportedAt > PEER_STALE_MS) {
    report("  Aun no se recibio aviso de intento TX de placa 2");
    return;
  }
  report("  Placa 2 escribio " + String(rs485ReportTrace.txBytes) +
         " B en su UART (informe " + String(rs485ReportTrace.reportId) + "):");
  const String expected = fromHex(rs485ReportTrace.expectedHex);
  size_t start = 0;
  while (start < expected.length()) {
    const int end = expected.indexOf("\r\n", start);
    if (end < 0) break;
    report("    enviado: " + expected.substring(start, end));
    start = static_cast<size_t>(end) + 2;
  }
  if (!rs485ReportTrace.started) {
    report("  RX placa 1: falta marca de inicio; no hay captura comparable");
    return;
  }
  if (!rs485ReportTrace.complete) {
    report("  RX placa 1: captura en curso");
    return;
  }
  report("  Placa 1 leyo durante ese envio: " +
         String(rs485ReportTrace.rxBytes) + " B");
  report("    recibido: " +
         (rs485ReportTrace.rxBytes == 0 ? String("<ningun byte>") :
          showHex(rs485ReportTrace.receivedHex)) +
         (rs485ReportTrace.rxBytes > RS485_SAMPLE_MAX_BYTES ?
          " [muestra cortada]" : ""));
  report("  Informes reconocidos por RS485: " +
         String(validRs485Reports(rs485ReportTrace.reportId)) + "/4");
}

const ChannelWindow* boardWindow(uint8_t board, uint8_t channel) {
  if (board == DEVICE_ID) return localReportId ? &localWindow[channel] : nullptr;
  const PeerCopy* copy = currentPeerCopy(channel);
  return copy ? &copy->window : nullptr;
}

const FlowWindow* boardFlow(uint8_t board, uint8_t channel) {
  if (board == DEVICE_ID) return localReportId ? &localFlow[channel] : nullptr;
  const PeerCopy* copy = currentPeerCopy(channel);
  return copy && peerFlow[channel].reportId == copy->reportId ?
    &peerFlow[channel].window : nullptr;
}

void reportBothBoards(uint8_t channel, bool detailed) {
  for (uint8_t board = 1; board <= 2; ++board) {
    report("--- PLACA " + String(board) + " ---");
    const ChannelWindow* window = boardWindow(board, channel);
    if (!window) {
      report("Sin informe reciente");
      continue;
    }
    const FrameComparison* frame = nullptr;
    if (board == DEVICE_ID) frame = &localComparison[channel][0];
    else {
      const PeerCopy* copy = currentPeerCopy(channel);
      const PeerFrameComparison& peer = peerComparison[channel][0];
      if (copy && peer.reportId == copy->reportId &&
          millis() - peer.receivedAt <= PEER_STALE_MS) frame = &peer.frame;
    }
    reportWindow(channel, board, *window, boardFlow(board, channel), frame);
    if (detailed && (!frame || !frame->seen ||
                     frame->verdict == 1 || frame->verdict == 4))
      reportComparison("Comparacion RX", frame);
    if (channel == 2 && window->invalid > 0)
      report("  NUL de esta ventana=" + String(window->invalid));
    if (channel == 3 && board == DEVICE_ID && window->invalid > 0)
      report("  Buffer CAN RX0 lleno=" + String(canOverflowRx0Window) +
             "; RX1 lleno=" + String(canOverflowRx1Window));
  }
}

uint8_t maskCount(uint8_t mask) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < CHANNEL_COUNT; ++i) if (mask & (1 << i)) ++count;
  return count;
}

void reportInfoDirections(uint8_t channel) {
  for (uint8_t sender = 1; sender <= 2; ++sender) {
    String summary = "INFO " + String(sender) + "->" +
                     String(sender == 1 ? 2 : 1) + ": ";
    if (sender == DEVICE_ID) {
      if (!localReportId) {
        summary += "sin informe";
      } else {
        const ReportReceipt& receipt = peerReceipts[channel];
        if (receipt.reportId == localReportId &&
            millis() - receipt.receivedAt <= PEER_STALE_MS)
          summary += "informe " + String(receipt.reportId) +
                     ", recibidos " + String(maskCount(receipt.mask)) + "/4";
        else
          summary += "informe " + String(localReportId) + ", confirmacion " +
                     (millis() - lastWindowCapturedAt < REPORT_SETTLE_MS + 3000 ?
                      "en camino" : "no recibida por TTL1");
      }
    } else if (latestPeerReportId && millis() - latestPeerAt <= PEER_STALE_MS) {
      const uint8_t count = maskCount(receivedInfoMask(channel, latestPeerReportId));
      summary += "informe " + String(latestPeerReportId) +
                 ", recibidos " + String(count) + "/4" +
                 (count < 4 && millis() - latestPeerFirstAt < REPORT_SETTLE_MS ?
                  " (en camino)" : "");
    } else {
      summary += "sin informe reciente";
    }
    report(summary);
  }
}

void reportDiagnosticHealth() {
  if (diagnosticRejected || diagnosticDropped)
    report("Datos auxiliares (desde arranque): rechazados=" + String(diagnosticRejected) +
           "; envios omitidos=" + String(diagnosticDropped) +
           " (detalle remoto puede estar incompleto)");
}

void printDetails() {
  report("********************************");
  report("DATOS - ultimos 10 s");
  if (!automaticTestsEnabled) report("PRUEBAS DETENIDAS");
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    report("======== " + String(CHANNEL_NAMES[channel]) + " ========");
    reportBothBoards(channel, false);
    reportInfoDirections(channel);
  }
  if (latestPeerReportId && millis() - latestPeerAt <= PEER_STALE_MS) {
    reportPeerCopyMatrix();
  }
  report("======== PULSOS ========");
  reportBoard2Pulses();
  reportDiagnosticHealth();
  report("********************************");
}

uint8_t traceHexByte(const String& hex, size_t offset) {
  const String pair = hex.substring(offset, offset + 2);
  return static_cast<uint8_t>(strtoul(pair.c_str(), nullptr, 16));
}

void reportRawTrace(uint8_t channel, const char* direction,
                    const RawTrace* trace, uint8_t reportId) {
  const String label = String(direction) +
                       (reportId ? " (informe " + String(reportId) + ")" : "");
  if (!trace) {
    report(label + ": sin muestra reciente (via TTL1)");
    return;
  }
  if (channel == 3) {
    const size_t shown = trace->hex.length() / 26;
    report(label + ": " + String(trace->count) + " tramas; " +
           String(shown) + " mostradas" +
           (trace->count > shown ? " (RECORTADO: ultimas)" : " (completo)"));
    if (shown == 0) {
      report("  <ninguna trama>");
      return;
    }
    for (size_t index = 0; index < shown; ++index) {
      const size_t start = index * 26;
      struct can_frame frame{};
      for (uint8_t byte = 0; byte < 4; ++byte)
        frame.can_id |= static_cast<uint32_t>(
          traceHexByte(trace->hex, start + byte * 2)) << (byte * 8);
      frame.can_dlc = traceHexByte(trace->hex, start + 8);
      for (uint8_t byte = 0; byte < 8; ++byte)
        frame.data[byte] = traceHexByte(trace->hex, start + 10 + byte * 2);
      report("  " + canFrameText(frame));
    }
  } else {
    const size_t shown = trace->hex.length() / 2;
    report(label + ": " + String(trace->count) + " B; " +
           String(shown) + " B mostrados" +
           (trace->count > shown ? " (RECORTADO: ultimos)" : " (completo)"));
    if (shown == 0) report("  <ningun byte>");
    for (size_t offset = 0; offset < shown; offset += 64) {
      const String chunk = trace->hex.substring(offset * 2, (offset + 64) * 2);
      report("  HEX: " + chunk);
      report("  TXT: " + showHex(chunk));
    }
  }
}

void reportBoardRaw(uint8_t channel, uint8_t board, bool withTx) {
  report("--- PLACA " + String(board) + " ---");
  if (board == DEVICE_ID) {
    if (withTx) reportRawTrace(channel, "TX", localReportId ?
                               &txTraceWindow[channel] : nullptr, localReportId);
    reportRawTrace(channel, "RX", localReportId ?
                    &rxTraceWindow[channel] : nullptr, localReportId);
    return;
  }
  const PeerRawTrace& tx = peerTxTrace[channel];
  const PeerRawTrace& rx = peerRxTrace[channel];
  if (withTx) reportRawTrace(channel, "TX",
    tx.reportId && millis() - tx.receivedAt <= PEER_STALE_MS ? &tx.trace : nullptr,
    tx.reportId);
  reportRawTrace(channel, "RX",
    rx.reportId && millis() - rx.receivedAt <= PEER_STALE_MS ? &rx.trace : nullptr,
    rx.reportId);
}

void reportChannelCopies(uint8_t via) {
  const uint8_t remote = DEVICE_ID == 1 ? 2 : 1;
  report("======== COPIAS INFO PLACA " + String(remote) + " VIA " +
         CHANNEL_NAMES[via] + " ========");
  if (!latestPeerReportId || millis() - latestPeerAt > PEER_STALE_MS) {
    report("Sin informe reciente para comparar");
    return;
  }
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const PeerCopy* reference = firstPeerCopy(channel);
    const PeerCopy& copy = peerCopies[via][channel];
    const bool received = latestPeerReportId != 0 &&
      copy.reportId == latestPeerReportId &&
      millis() - copy.receivedAt <= PEER_STALE_MS;
    const FrameComparison& live = liveComparison[via][1 + channel];
    const FrameComparison& saved = localComparison[via][1 + channel];
    const FrameComparison* malformed = latestPeerReportId != 0 &&
      live.seen && live.verdict == 3 &&
      live.reportId == latestPeerReportId ? &live :
      latestPeerReportId != 0 && saved.seen && saved.verdict == 3 &&
      saved.reportId == latestPeerReportId ? &saved : nullptr;
    report("  " + String(CHANNEL_NAMES[channel]) + ": " +
           (received ? (reference == &copy ? "REFERENCIA" :
                        reference && sameWindow(reference->window, copy.window)
                        ? "IGUAL" : "DIFERENTE") :
            malformed ? "TRAMA INVALIDA" :
            latestPeerReportId && millis() - latestPeerFirstAt < REPORT_SETTLE_MS ?
            "EN CAMINO" : "NO LLEGA"));
    report("    esperado: " +
           (reference ? peerInfoText(channel, *reference) : malformed ?
            malformed->expected :
            String("sin copia de referencia")));
    report("    recibido: " +
           (received ? peerInfoText(channel, copy) : malformed ?
            showHex(toHex(malformed->received)) : String("<ninguna trama valida>")));
  }
}

void printChannelDetails(uint8_t channel) {
  report("********************************");
  report(String(CHANNEL_NAMES[channel]) +
         " - DIAGNOSTICO COMPLETO (ultimos 10 s)");
  reportBothBoards(channel, true);
  reportInfoDirections(channel);
  report("======== TX/RX CRUDOS ========");
  reportBoardRaw(channel, 1, true);
  reportBoardRaw(channel, 2, true);
  reportChannelCopies(channel);
  if (channel == 2 && DEVICE_ID == 1) {
    report("======== INFORME RS485 2->1 ========");
    reportRs485Exchange();
  }
  reportDiagnosticHealth();
  report("********************************");
}

void printHelp() {
  report("COMANDOS: STATUS, DATOS, TTL1, TTL2, RS485, CAN, RAW, RUN, STOP, HELP");
  report("STATUS: resumen local; DATOS o DATA: detalle y placa remota");
  report("TTL1/TTL2/RS485/CAN: tramas y lecturas crudas por canal");
  report("RAW: RX de los 4 canales y ambas placas; RUN/STOP: iniciar o detener");
  report("DATOS: PING y PONG separados; PONG correctos confirma ida y vuelta");
  report("UART0/TTL2 usa GPIO3/1: desconectar USB durante la prueba");
}

void printData() {
  report("********************************");
  report("RAW - RX DE AMBAS PLACAS");
  report("Ultima ventana disponible; incluye datos invalidos y auxiliares");
  report("Limite por placa/canal: 1024 bytes UART o 32 tramas CAN");
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    report("======== " + String(CHANNEL_NAMES[channel]) + " ========");
    reportBoardRaw(channel, 1, false);
    reportBoardRaw(channel, 2, false);
  }
  reportDiagnosticHealth();
  report("********************************");
}

void processBluetoothCommand(const String& rawCommand) {
  String command = rawCommand;
  command.trim();
  command.toUpperCase();
  if (command == "STATUS") {
    printBriefStatus();
  } else if (command == "DATA" || command == "DATOS") {
    printDetails();
  } else if (command == "TTL1") {
    printChannelDetails(0);
  } else if (command == "TTL2") {
    printChannelDetails(1);
  } else if (command == "RS485") {
    printChannelDetails(2);
  } else if (command == "CAN") {
    printChannelDetails(3);
  } else if (command == "RAW") {
    printData();
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
  Serial.setRxBufferSize(4096);
  ttl1.setRxBufferSize(4096);
  rs485.setRxBufferSize(2048);
  Serial.setTxBufferSize(2048);
  ttl1.setTxBufferSize(2048);
  Serial.begin(LINK_BAUD, SERIAL_8N1, TTL2_RX, TTL2_TX);
  ttl1.begin(LINK_BAUD, SERIAL_8N1, TTL1_RX, TTL1_TX);
  rs485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);

  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
  canController.reset();
  canController.setBitrate(CAN_250KBPS, MCP_8MHZ);
  canController.setNormalMode();

  bluetooth.begin("HWTEST-ESP32-" + String(DEVICE_ID));
  delay(300);
  lastStatusAt = lastWindowCapturedAt = millis();
  if (DEVICE_ID == 2) {
    pulseRateSampleAt = lastStatusAt;
    pinMode(PULSE_1, INPUT_PULLUP);
    pinMode(PULSE_2, INPUT);  // GPIO35 no dispone de pull-up interno.
    attachInterrupt(digitalPinToInterrupt(PULSE_1), onPulse1, RISING);
    attachInterrupt(digitalPinToInterrupt(PULSE_2), onPulse2, RISING);
  }
  report("HWTEST listo. Placa " + String(DEVICE_ID));
  report("Conecte ambas placas y use STATUS o HELP");
}

void loop() {
  samplePulseRate();
  pollBluetooth();
  pollLink(ttl1, ttl1Buffer, ttl1Stats, false);
  pollLink(ttl2, ttl2Buffer, ttl2Stats, false);
  pollLink(rs485, rs485Buffer, rs485Stats, true);
  if (DEVICE_ID == 1 && rs485ReportTrace.txReported &&
      !rs485ReportTrace.complete)
    rs485ReportTrace.complete = true;  // La UART RS485 ya se dreno tras RTX.
  if (DEVICE_ID == 1 && rs485ReportTrace.started &&
      millis() - rs485ReportTrace.startedAt > 2000)
    rs485ReportTrace.complete = true;
  pollCan();
  processCanInfoQueue();

  if (DEVICE_ID == 2 && rs485ReportDue != 0 &&
      (!rs485AwaitingReady &&
       static_cast<int32_t>(millis() - rs485ReportDue) >= 0)) {
    rs485AwaitingReady = true;
    rs485ReadyReportId = 0;
    rs485AnnouncedReportId = localReportId;
    rs485ReportDue = millis() + 1000;  // Limite si no llega confirmacion TTL1.
    sendDiagnostic("RBEGIN,2," + String(rs485AnnouncedReportId), true);
  }
  if (DEVICE_ID == 2 && rs485AwaitingReady &&
      localReportId != rs485AnnouncedReportId) {
    rs485ReadyReportId = 0;
    rs485AnnouncedReportId = localReportId;
    rs485ReportDue = millis() + 1000;
    sendDiagnostic("RBEGIN,2," + String(rs485AnnouncedReportId), true);
  }
  if (DEVICE_ID == 2 && rs485AwaitingReady &&
      (rs485ReadyReportId == rs485AnnouncedReportId ||
       static_cast<int32_t>(millis() - rs485ReportDue) >= 0)) {
    rs485AwaitingReady = false;
    rs485ReportDue = 0;
    String expected;
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      expected += infoLine(channel);
      expected += "\r\n";
    }
    const uint32_t before = infoBytesSent[2];
    sendInfoSerial(rs485, true);
    sendDiagnostic("RTX,2," + String(localReportId) + "," +
                 String(infoBytesSent[2] - before) + "," +
                 toHexComplete(expected), true);
  }

  const uint32_t now = millis();
  if (automaticTestsEnabled && now - lastPingAt >= PING_INTERVAL_MS) {
    ++nextSequence;
    const String ttl1Message = "PING," + String(DEVICE_ID) + "," + String(nextSequence);
    const String ttl2Message = ttl1Message;
    sendLine(ttl1, ttl1Stats, ttl1Message);
    sendLine(ttl2, ttl2Stats, ttl2Message);
    if (DEVICE_ID == 1 && rs485ReportTrace.started &&
        !rs485ReportTrace.complete)
      rs485PingDeferred = true;  // Evitar solapar el PING con el informe.
    else {
      sendRs485Ping();
      rs485PingDeferred = false;
    }
    sendCanPing();
    lastPingAt = now;
  }
  if (DEVICE_ID == 1 && rs485PingDeferred &&
      rs485ReportTrace.complete) {
    rs485PingDeferred = false;
    if (automaticTestsEnabled) sendRs485Ping();
  }

  checkTimeout(ttl1Stats);
  checkTimeout(ttl2Stats);
  checkTimeout(rs485Stats);

  if (now - lastStatusAt >= STATUS_INTERVAL_MS) {
    captureAllWindows();
    if (DEVICE_ID == 1) sendInfoSerial(rs485, true);
    sendInfoSerial(ttl1, false);
    sendInfoSerial(ttl2, false);
    sendInfoCan();
    statusPrintDue = millis() + 1200;
    lastStatusAt = now;
  }
  if (statusPrintDue != 0 && static_cast<int32_t>(millis() - statusPrintDue) >= 0) {
    statusPrintDue = 0;
    printBriefStatus();
  }
  processReceipts();
  processDiagnosticOutput();
  processBluetoothOutput();
  delay(2);
}
