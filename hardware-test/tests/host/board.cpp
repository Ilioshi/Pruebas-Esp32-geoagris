#include "Arduino.h"
#if DEVICE_ID == 1
#define BOARD_NAMESPACE firmware1
#else
#define BOARD_NAMESPACE firmware2
#endif
namespace BOARD_NAMESPACE {
HardwareSerial Serial(0);
SPIClass SPI;
#include "../../src/main.cpp"
}

#if DEVICE_ID == 1
#define HOST_FACTORY hostBoard1
#else
#define HOST_FACTORY hostBoard2
#endif

HostBoard HOST_FACTORY() {
  using namespace BOARD_NAMESPACE;
  return { setup, loop, {&ttl1, &ttl2, &rs485}, &canController, &bluetooth,
    [] { return int(auxRoute); },
    [] { return DEVICE_ID == 1 ? peerPulseRate1 : localPulseRate1; },
    [] { for (int ch = 0; ch < 4; ++ch) if (!peerRxTrace[ch].reportId) return false;
         return true; },
    [] { return auxRequestId; },
    [] { return ttl1Stats.ok; },
    [] { return rs485Stats.ok; },
    [] { return canOk; },
    [](uint8_t via) { return receivedInfoMask(via, latestPeerReportId); },
    processBluetoothCommand, onPulse1,
    [] { auxSession += 10000; auxNextSequence = 0; auxFlight = AuxFlight{};
         auxLastReceived = 0; auxPeerSession = 0; diagnosticQueue.clear();
         diagnosticDetailQueue.clear(); },
    [] { return auxLastReceived; },
    [] { for (uint8_t ch = 0; ch < 4; ++ch) {
           for (RawTrace* trace : {&txTraceWindow[ch], &rxTraceWindow[ch]}) {
             *trace = RawTrace{};
             if (ch < 3) {
               for (int i = 0; i < 1024; ++i) traceByte(*trace, 'A');
             } else {
               can_frame frame{}; frame.can_id = 0x101; frame.can_dlc = 4;
               frame.data[0] = 1; frame.data[3] = 0xA5;
               for (int i = 0; i < 32; ++i) traceCanFrame(*trace, frame);
             }
           }
         } }
  };
}
