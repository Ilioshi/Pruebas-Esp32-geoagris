#pragma once

#include <stdint.h>
#include <string>

// CAN producers only update a bounded memory snapshot, never the UART.
void dutCanInitResult(uint8_t reset, uint8_t bitrate, uint8_t filters, uint8_t mode);
void dutCanReceived(uint32_t id, uint8_t dlc);
void dutCanDecoded(uint32_t pgn);
void dutCanHealth(uint8_t flags, uint8_t rec, uint8_t tec, bool readError, bool overflow);
void dutCanLoop();
// These three functions are called exclusively from sendTask.
void dutStx08State(const char* state, uint32_t seq);
bool dutStx08Transaction(uint32_t seq, const std::string& frame, const std::string& response);
void dutCanDiagnosticTick();
