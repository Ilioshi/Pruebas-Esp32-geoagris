#pragma once

#include "CAN/CAN_protocols/j1939_protocol.h"
#include "CAN/CAN_protocols/isobus_protocol.h"
#include "CAN/CAN.h"
#include <string>

class SendTaskCanJ1939 {
public:
    static bool buildMessage(const J1939Data& data, uint32_t nowMs, std::string& outMessage);
};

class SendTaskCanIsobus {
public:
    static bool buildMessage1(const IsobusData& data, uint32_t nowMs, std::string& outMessage);
    static bool buildMessage2(const IsobusData& data, uint32_t nowMs, std::string& outMessage);
};

class SendTaskCanVg55r {
public:
    static bool buildMessage(const Vg55rData& data, std::string& outMessage);
};
