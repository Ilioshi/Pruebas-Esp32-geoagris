#ifdef ENABLE_BEACON

#pragma once

#include "BeaconTask.h"
#include <string>

class SendTaskBeacon {
public:
    static std::string buildStx13(const BeaconReportData& data);
};

#endif // ENABLE_BEACON