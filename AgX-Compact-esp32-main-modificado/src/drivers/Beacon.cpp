#ifdef ENABLE_BEACON

#include "Beacon.h"
#include <BLEScan.h>
#include <math.h>
#include <vector>

// ============================================================================
// BeaconMath
// ============================================================================

float BeaconMath::calculateDistance(int8_t rssi, int8_t txPower, float pathLossExponent) {
    if (rssi >= 0) return -1.0f;
    float ratio = static_cast<float>(txPower - rssi) / (10.0f * pathLossExponent);
    return powf(10.0f, ratio);
}

// ============================================================================
// BeaconAccumulator
// ============================================================================

bool BeaconAccumulator::parseIBeacon(const std::string& mfg, int8_t& txPower) {
    // Verificar tamaño mínimo
    if (mfg.length() < IBEACON_MIN_PACKET_SIZE) 
        return false;
    
    // Verificar tipo y longitud de iBeacon
    if ((uint8_t)mfg[2] != IBEACON_TYPE || (uint8_t)mfg[3] != IBEACON_DATA_LENGTH)
        return false;
    
    // Extraer TX Power (último byte)
    txPower = (int8_t)mfg[24];
    return true;
}

void BeaconAccumulator::processScan(BLEScanResults* results, int8_t rssiThreshold) {
    if (results == nullptr) return;
    
    ++scansProcessed;
    int deviceCount = results->getCount();
    
    for (int i = 0; i < deviceCount; i++) {
        BLEAdvertisedDevice device = results->getDevice(i);
        
        // Filtros rápidos
        if (!device.haveManufacturerData()) continue;
        if (device.getRSSI() < rssiThreshold) continue;
        
        // Verificar si es iBeacon y obtener txPower
        int8_t txPower;
        if (!parseIBeacon(device.getManufacturerData(), txPower)) continue;
        
        // Acumular datos
        String mac = String(device.getAddress().toString().c_str());
        BeaconData& data = beacons[mac];
        
        if (data.samples == 0) {
            data.macAddress = mac;
            data.txPower = txPower;
        }
        
        data.rssiSum += device.getRSSI();
        data.samples++;
    }
}

void BeaconAccumulator::clear() {
    beacons.clear();
    scansProcessed = 0;
}

std::vector<std::pair<String, float>> BeaconAccumulator::getNearestWithDistance(int maxBeacons) const {
    std::vector<std::pair<String, float>> sorted;
    if (beacons.empty()) return sorted;

    sorted.reserve(beacons.size());

    for (const auto& pair : beacons) {
        const BeaconData& data = pair.second;
        int8_t avgRssi = (int8_t)roundf(data.rssiSum / data.samples);
        float distance = BeaconMath::calculateDistance(avgRssi, data.txPower);
        if (distance > 0) {
            sorted.push_back({data.macAddress, distance});
        }
    }

    std::sort(sorted.begin(), sorted.end(), [](const std::pair<String, float>& a, const std::pair<String, float>& b) {
        return a.second < b.second;
    });

    if (sorted.size() > (size_t)maxBeacons) {
        sorted.resize(maxBeacons);
    }
    return sorted;
}

#endif // ENABLE_BEACON