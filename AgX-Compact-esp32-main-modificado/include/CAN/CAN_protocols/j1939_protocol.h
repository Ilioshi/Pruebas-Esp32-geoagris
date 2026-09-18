#ifndef J1939_PROTOCOL_H
#define J1939_PROTOCOL_H

#include <Arduino.h>
#include <freertos/semphr.h>

// Datos crudos J1939 entregados por canReadTask
struct J1939Data {
    bool hasRpm = false;
    uint16_t rpm = 0;
    uint32_t rpmTs = 0;

    bool hasTorque = false;
    int16_t torque = 0;
    uint32_t torqueTs = 0;

    bool hasFuelConsumption = false;
    float fuelConsumption = 0.0f;
    uint32_t fuelConsumptionTs = 0;

    bool hasEngineTemperature = false;
    int16_t engineTemperature = 0;
    uint32_t engineTemperatureTs = 0;

    bool hasEngineHours = false;
    uint32_t engineHours = 0;
    uint32_t engineHoursTs = 0;
};

// Inicializa el módulo J1939 (mutex y estructuras de datos)
extern SemaphoreHandle_t j1939DataMutex;

// Procesa mensajes CAN entrantes para J1939
void j1939ProcessMessage(long unsigned int rxId, unsigned char *rxBuf, unsigned char len);

// Devuelve datos parseados si hay cambios
bool j1939GetData(J1939Data& out, uint32_t& seq);

#endif // J1939_PROTOCOL_H
