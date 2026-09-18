#ifndef CAN_H
#define CAN_H

#include <Arduino.h>

// Datos crudos VG55R entregados por canReadTask
struct Vg55rData {
    bool valid = false;
    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;
    uint32_t timestamp = 0;
};

// Funciones principales del módulo CAN
void canReadTask(void *pvParameters);

bool canGetVg55rData(Vg55rData& out, uint32_t& seq);

#endif // CAN_H
