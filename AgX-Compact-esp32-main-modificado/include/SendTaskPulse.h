#pragma once

#include "Pulse.h"
#include <string>

class SendTaskPulse {
public:
    static std::string buildStx13(const PulseData& data);
};
