#pragma once
#include <Arduino.h>

// Datos de pulsos entregados por Pulse (ventana de muestreo)
struct PulseData {
    uint32_t pulses1 = 0;
    uint32_t pulses2 = 0;
    uint32_t elapsedMs = 0; // ventana de integración
    float rate1 = 0.0f;
    float rate2 = 0.0f;
    uint64_t total1 = 0;
    uint64_t total2 = 0;
};

void initPulseCounters();
void pulseUpdateIfDue();
bool pulseGetData(PulseData& out, uint32_t& seq);
