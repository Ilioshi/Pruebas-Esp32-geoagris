// Minimal helpers para trabajar con iBeacons sin depender de las libs BLE:
// filtrar, calcular distancia, acumular múltiples mediciones y ordenar.
#ifndef BEACON_H
#define BEACON_H

#ifdef ENABLE_BEACON

#include <Arduino.h>
#include <map>
#include <vector>
#include <string>
#include <stdint.h>

class BLEScanResults; // Forward declaration
class BLEAdvertisedDevice; // Forward declaration

// iBeacon Protocol (Apple Specification)
#define IBEACON_APPLE_COMPANY_ID    0x004C
#define IBEACON_TYPE                0x02
#define IBEACON_DATA_LENGTH         0x15
#define IBEACON_MIN_PACKET_SIZE     25

// Defaults
#define IBEACON_DEFAULT_RSSI_THRESHOLD  -100
#define IBEACON_PATH_LOSS_DEFAULT       2.0f

// Información mínima de un beacon detectado
struct BeaconData {
    String macAddress;
    int8_t txPower;
    float rssiSum;      // Suma de RSSI para promediar
    uint32_t samples;   // Cantidad de veces detectado
    
    BeaconData() : txPower(-59), rssiSum(0.0f), samples(0) {}
};

class BeaconMath {
public:
    static float calculateDistance(int8_t rssi, int8_t txPower, float pathLossExponent = IBEACON_PATH_LOSS_DEFAULT);
};

// Procesa scans de BLE y acumula beacons detectados
class BeaconAccumulator {
public:
    // Procesa un scan completo
    void processScan(BLEScanResults* results, int8_t rssiThreshold = IBEACON_DEFAULT_RSSI_THRESHOLD);
    
    std::vector<std::pair<String, float>> getNearestWithDistance(int maxBeacons = 5) const;
    
    // Limpia el acumulador
    void clear();
    
    uint32_t scanCount() const { return scansProcessed; }
    
private:
    // Verifica si es un iBeacon válido y extrae txPower
    bool parseIBeacon(const std::string& manufacturerData, int8_t& txPower);
    
    // Mapa: MAC -> datos del beacon
    std::map<String, BeaconData> beacons;
    uint32_t scansProcessed = 0;
};

#endif // ENABLE_BEACON

#endif // BEACON_H