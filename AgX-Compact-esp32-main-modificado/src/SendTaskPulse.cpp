#include "SendTaskPulse.h"
#include <cstdio>

std::string SendTaskPulse::buildStx13(const PulseData& data) {
    char buf[128];
    snprintf(buf, sizeof(buf), ">STX13,PLS:%.3f:%.3f:%llu:%llu<",
             data.rate1, data.rate2,
             static_cast<unsigned long long>(data.total1),
             static_cast<unsigned long long>(data.total2));
    return std::string(buf);
}
