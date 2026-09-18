#pragma once
#include <Arduino.h>
#include "trax_utils.h"

// Datos de presión entregados por PressureTask
struct PressureData {
    bool valid = false;
    float pressure = 0.0f;
};

// Tarea para monitorear el sensor de presión y reportar sus lecturas
void pressureTask(void* parameter);

bool pressureGetData(PressureData& out, uint32_t& seq);
