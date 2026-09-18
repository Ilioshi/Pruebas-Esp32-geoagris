#include "DutCanDiagnostic.h"
#include "DiagnosticProtocol.h"
#include "trax_utils.h"
#include <Arduino.h>
#include <cstdio>

#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
#include "DiagnosticProtocolSelfTest.h"
#pragma message("DUT18v2: CAN heartbeat/ages + validated STX08; no reset on error-passive; BLE unchanged")
namespace {
struct CanSnapshot {
    uint32_t attempts = 0, rx = 0, extended = 0, lastId = 0;
    uint32_t rpm = 0, fuel = 0, temp = 0, hours = 0, overflows = 0, readErrors = 0;
    uint32_t loops = 0, loopMs = 0, rxMs = 0;
    uint8_t reset = 255, bitrate = 255, filters = 255, mode = 255;
    uint8_t dlc = 0, flags = 0, rec = 0, tec = 0;
};
CanSnapshot can;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
// Owned only by sendTask, not shared with CAN callbacks.
const char* stxState = "BOOT";
uint32_t stxSeq = 0, stxAttempts = 0, stxAcks = 0;
std::string lastFrame, lastResponse;
uint32_t lastDiagnostic = 0, diagnosticAcks = 0;
bool sendCan = true;
uint32_t diagnosticSeq = 0;
}
#endif

void dutCanLoop() {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    portENTER_CRITICAL(&snapshotMux);
    ++can.loops;
    can.loopMs = millis();
    portEXIT_CRITICAL(&snapshotMux);
#endif
}

void dutCanInitResult(uint8_t reset, uint8_t bitrate, uint8_t filters, uint8_t mode) {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    portENTER_CRITICAL(&snapshotMux);
    ++can.attempts;
    can.reset = reset; can.bitrate = bitrate; can.filters = filters; can.mode = mode;
    portEXIT_CRITICAL(&snapshotMux);
#endif
}

void dutCanReceived(uint32_t id, uint8_t dlc) {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    portENTER_CRITICAL(&snapshotMux);
    ++can.rx;
    can.rxMs = millis();
    if (id & 0x80000000UL) ++can.extended;
    can.lastId = id; can.dlc = dlc;
    portEXIT_CRITICAL(&snapshotMux);
#endif
}

void dutCanDecoded(uint32_t pgn) {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    portENTER_CRITICAL(&snapshotMux);
    if (pgn == 0xF004) ++can.rpm;
    if (pgn == 0xFEF2) ++can.fuel;
    if (pgn == 0xFEEE) ++can.temp;
    if (pgn == 0xFEE5) ++can.hours;
    portEXIT_CRITICAL(&snapshotMux);
#endif
}

void dutCanHealth(uint8_t flags, uint8_t rec, uint8_t tec, bool readError, bool overflow) {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    portENTER_CRITICAL(&snapshotMux);
    can.flags = flags; can.rec = rec; can.tec = tec;
    if (readError) ++can.readErrors;
    if (overflow) ++can.overflows;
    portEXIT_CRITICAL(&snapshotMux);
#endif
}

void dutStx08State(const char* state, uint32_t seq) {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    stxState = state;
    stxSeq = seq;
#endif
}

bool dutStx08Transaction(uint32_t seq, const std::string& frame, const std::string& response) {
    const bool acknowledged = DiagnosticProtocol::stx08Acknowledged(frame, response);
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    ++stxAttempts;
    if (acknowledged) ++stxAcks;
    stxSeq = seq;
    stxState = acknowledged ? "ACK" : response.empty() ? "TIMEOUT" : "BAD_REPLY";
    lastFrame = frame;
    lastResponse = response;
#endif
    return acknowledged;
}

void dutCanDiagnosticTick() {
#if defined(DUT_CAN_TR0_DIAG) && DUT_CAN_TR0_DIAG
    static const unsigned protocolTest = diagnosticProtocolSelfTest();
    const uint32_t now = millis();
    // One TX18 write per >=1.1s, alternating CAN and STX08 snapshots. TRAX ED
    // evaluates once/s; a per-frame stream would overwrite this single buffer.
    if (now - lastDiagnostic < 1100) return;
    lastDiagnostic = now;
    ++diagnosticSeq;
    char text[384];
    if (sendCan) {
        portENTER_CRITICAL(&snapshotMux);
        const CanSnapshot s = can;
        portEXIT_CRITICAL(&snapshotMux);
        // A CAN update may run after 'now' was sampled on the other core.
        const uint32_t sampleNow = millis();
        const bool ready = s.reset == 0 && s.bitrate == 0 && s.filters == 0 && s.mode == 0;
        snprintf(text, sizeof(text),
            "DUT18v2,N=%lu,UP=%lu,HB=%lu,HA=%lu,RA=%lu,TEST=%u,CFG=%s,I=%u/%u/%u/%u,INIT=%lu,RX=%lu,PGN=%lu/%lu/%lu/%lu,E=%02X,REC=%u,TEC=%u,OV=%lu,RDERR=%lu",
            (unsigned long)diagnosticSeq, (unsigned long)(now / 1000),
            (unsigned long)s.loops,
            (unsigned long)(s.loops ? sampleNow - s.loopMs : UINT32_MAX),
            (unsigned long)(s.rx ? sampleNow - s.rxMs : UINT32_MAX),
            protocolTest, ready ? "OK" : "FAIL", s.reset, s.bitrate, s.filters, s.mode,
            (unsigned long)s.attempts, (unsigned long)s.rx, (unsigned long)s.rpm, (unsigned long)s.fuel,
            (unsigned long)s.temp, (unsigned long)s.hours, s.flags, s.rec, s.tec,
            (unsigned long)s.overflows, (unsigned long)s.readErrors);
    } else {
        const std::string data = lastFrame.empty() ? "NONE" : lastFrame.substr(7, 23);
        const std::string replyHex = DiagnosticProtocol::hex(lastResponse, 64);
        snprintf(text, sizeof(text),
            "DUT18v2,N=%lu,UP=%lu,STX08=%s,J=%lu,TX=%lu,ACK=%lu,DACK=%lu,DATA=%s,RLEN=%u,RHEX=%s",
            (unsigned long)diagnosticSeq, (unsigned long)(now / 1000),
            stxState, (unsigned long)stxSeq, (unsigned long)stxAttempts,
            (unsigned long)stxAcks, (unsigned long)diagnosticAcks, data.c_str(),
            (unsigned)lastResponse.size(), replyHex.empty() ? "NONE" : replyHex.c_str());
    }
    const std::string request = DiagnosticProtocol::command(text);
    // Read this command's reply as a complete transaction; never leave RTX18
    // in UART0 for a weather/STX08 request to mistake for its own ACK.
    const std::string response = traxSendReceive(request);
    std::string expected = request.substr(0, request.size() - 1);
    expected[1] = 'R';
    if (DiagnosticProtocol::validFrame(response) && response.compare(0, expected.size(), expected) == 0 &&
        (response[expected.size()] == ';' || response[expected.size()] == '<')) ++diagnosticAcks;
    sendCan = !sendCan;
#endif
}
