#ifndef BEACON_TASK_H
#define BEACON_TASK_H

#ifdef ENABLE_BEACON

#include <Arduino.h>
#include <string>
#include <vector>
#include "AgxBle.h"

// Datos de beacons entregados por BeaconTask
struct BeaconNearestEntry {
    std::string mac;
    float distance = 0.0f;
};

struct BeaconReportData {
    std::vector<BeaconNearestEntry> nearest;
};

void beaconTask(void* parameter);
bool beaconGetData(BeaconReportData& out, uint32_t& seq);

#endif // ENABLE_BEACON

#endif // BEACON_TASK_H