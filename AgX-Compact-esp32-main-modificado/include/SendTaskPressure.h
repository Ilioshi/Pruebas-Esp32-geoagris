#pragma once

#include "PressureTask.h"
#include <string>

class SendTaskPressure {
public:
    static std::string buildStx09(const PressureData& data);
};
