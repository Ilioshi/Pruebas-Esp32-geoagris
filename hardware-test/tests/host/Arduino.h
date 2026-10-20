#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

class String {
  std::string s;
public:
  String() = default;
  String(const char* x): s(x ? x : "") {}
  String(char* x): s(x ? x : "") {}
  String(const std::string& x): s(x) {}
  String(char x): s(1, x) {}
  template<class T> String(T x): s(std::to_string(x)) {}
  template<class T> String(T x, int base) {
    std::ostringstream stream;
    if (base == 16) stream << std::hex;
    stream << x;
    s = stream.str();
  }
  size_t length() const { return s.length(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](size_t i) const { return s[i]; }
  bool startsWith(const String& x) const { return s.rfind(x.s, 0) == 0; }
  int indexOf(const String& x, size_t at = 0) const {
    auto n = s.find(x.s, at); return n == std::string::npos ? -1 : int(n);
  }
  int lastIndexOf(char x) const {
    auto n = s.rfind(x); return n == std::string::npos ? -1 : int(n);
  }
  String substring(size_t a, size_t b = std::string::npos) const {
    if (a > s.size()) return "";
    return s.substr(a, b == std::string::npos ? b : b - a);
  }
  void remove(size_t a, size_t n = std::string::npos) { s.erase(a, n); }
  void reserve(size_t n) { s.reserve(n); }
  long toInt() const { return std::strtol(s.c_str(), nullptr, 10); }
  void trim() {
    auto a = s.find_first_not_of(" \r\n\t");
    if (a == std::string::npos) { s.clear(); return; }
    s = s.substr(a, s.find_last_not_of(" \r\n\t") - a + 1);
  }
  void toUpperCase() { for (char& c: s) c = std::toupper(static_cast<unsigned char>(c)); }
  String& operator+=(const String& x) { s += x.s; return *this; }
  friend String operator+(const String& a, const String& b) { return a.s + b.s; }
  friend bool operator==(const String& a, const String& b) { return a.s == b.s; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};

inline uint32_t simNow = 1;
inline bool links[4][2] = {{true,true},{true,true},{true,true},{true,true}};
inline std::function<bool(int, int, std::string&)> serialFilter;
inline std::function<bool(int, std::vector<uint8_t>&)> canFilter;
inline uint32_t millis() { return simNow; }
inline void delay(uint32_t n) { simNow += n; }
inline void delayMicroseconds(uint32_t) {}
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline void noInterrupts() {}
inline void interrupts() {}
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
inline int digitalPinToInterrupt(int n) { return n; }
inline void attachInterrupt(int,void (*)(),int) {}
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define RISING 1
#define SERIAL_8N1 0
#define HEX 16

class Stream {
public:
  std::deque<uint8_t> input;
  virtual ~Stream() = default;
  int available() { return input.size(); }
  int read() { int n = input.front(); input.pop_front(); return n; }
};

class HardwareSerial: public Stream {
  std::string pending;
public:
  int uart, board = 0;
  HardwareSerial* peer = nullptr;
  explicit HardwareSerial(int n): uart(n) {}
  void setRxBufferSize(int) {}
  void setTxBufferSize(int) {}
  void begin(int,int,int,int) {}
  int availableForWrite() { return 2048; }
  void flush() {}
  template<class T> size_t print(const T& x) {
    String text(x);
    pending += text.c_str();
    return text.length();
  }
  template<class T> size_t println(const T& x) {
    size_t n = print(x);
    pending += "\r\n";
    int via = uart == 2 ? 0 : uart == 0 ? 1 : 2;
    std::string packet = pending;
    pending.clear();
    if (peer && links[via][board] && (!serialFilter || serialFilter(via, board, packet)))
      for (unsigned char c: packet) peer->input.push_back(c);
    return n + 2;
  }
};

class BluetoothSerial: public Stream {
public:
  std::vector<std::string> output;
  bool hasClient() { return true; }
  void begin(const String&) {}
  void println(const String& s) { output.emplace_back(s.c_str()); }
};
struct SPIClass { void begin(int,int,int,int) {} };
struct can_frame { uint32_t can_id; uint8_t can_dlc; uint8_t data[8]; };
class MCP2515 {
public:
  enum ERROR { ERROR_OK, ERROR_FAIL };
  int board = 0;
  MCP2515* peer = nullptr;
  std::deque<can_frame> input;
  uint8_t flags = 0;
  explicit MCP2515(int) {}
  void reset() {}
  void setBitrate(int,int) {}
  void setNormalMode() {}
  ERROR sendMessage(const can_frame* f) {
    if (!peer || !links[3][board]) return ERROR_FAIL;
    std::vector<uint8_t> bytes(f->data, f->data + 8);
    if (canFilter && !canFilter(board, bytes)) return ERROR_OK;
    auto frame = *f;
    std::copy(bytes.begin(), bytes.end(), frame.data);
    if (peer->input.size() >= 2) { peer->flags |= 0x40; return ERROR_OK; }
    peer->input.push_back(frame);
    return ERROR_OK;
  }
  ERROR readMessage(can_frame* f) {
    if (input.empty()) return ERROR_FAIL;
    *f = input.front(); input.pop_front(); return ERROR_OK;
  }
  uint8_t getErrorFlags() { return flags; }
  void clearRXnOVRFlags() { flags = 0; }
};
#define CAN_250KBPS 0
#define MCP_8MHZ 0
inline uint32_t esp_random() { static uint32_t n = 123456; return ++n; }

struct HostBoard {
  void (*setup)();
  void (*loop)();
  HardwareSerial* ports[3];
  MCP2515* can;
  BluetoothSerial* bluetooth;
  int (*route)();
  uint32_t (*pulse)();
  bool (*rawComplete)();
  uint32_t (*rawRequest)();
  uint32_t (*ttlOk)();
  uint32_t (*rs485Ok)();
  uint32_t (*canOk)();
  uint8_t (*infoMask)(uint8_t);
  void (*command)(const String&);
  void (*addPulse)();
  void (*restartSession)();
  uint32_t (*rxSequence)();
  void (*fillCaptures)();
};
HostBoard hostBoard1();
HostBoard hostBoard2();
