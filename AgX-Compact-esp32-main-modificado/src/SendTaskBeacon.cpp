#ifdef ENABLE_BEACON

#include "SendTaskBeacon.h"
#include <algorithm>
#include <cstdio>

std::string SendTaskBeacon::buildStx13(const BeaconReportData& data) {
    if (data.nearest.empty()) return "";
    std::string msg = ">STX13,BCS";
    for (const auto& b : data.nearest) {
        std::string macClean = b.mac;
        macClean.erase(std::remove(macClean.begin(), macClean.end(), ':'), macClean.end());
        char buf[32];
        snprintf(buf, sizeof(buf), ":%s:%.2f", macClean.c_str(), b.distance);
        msg += buf;
    }
    msg += "<";
    return msg;
}

#endif // ENABLE_BEACON