#pragma once
#include <Arduino.h>
#include <string>
#include "trax_utils.h"

// Datos crudos de clima entregados por WeatherTask
struct WeatherData {
    bool valid = false;
    std::string gpsTime;
    double gpsSpeedKmh = 0.0;
    double gpsHeading = 0.0;
    double windSpeed = 0.0;
    double pressure = 0.0;
    double airTemp = 0.0;
    double relativeHumidity = 0.0;
    double windDirection = 0.0;
    double compass = 0.0;
    double precipitation = 0.0;
};

// La función principal que se ejecuta como tarea FreeRTOS para monitoreo meteorológico
void weatherTask(void* parameter);

bool weatherGetData(WeatherData& out, uint32_t& seq);
