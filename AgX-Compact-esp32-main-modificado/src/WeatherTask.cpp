#include "WeatherTask.h"
#include "rs485_utils.h"
#include "WeatherStation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Variable para depuración
static bool G_debug_weather = false;

static bool G_tolerate_no_gps = true;

static SemaphoreHandle_t weatherMutex = NULL;
static WeatherData weatherState;
static uint32_t weatherSeq = 0;

// Función para imprimir mensajes de depuración
static void printForDebugWeather(const std::string& message) {
    if (G_debug_weather) {
        std::string msg = message;
        size_t pos;
        // Reemplazar '>' con '}'
        while ((pos = msg.find('>')) != std::string::npos) {
            msg.replace(pos, 1, "}");
        }
        // Reemplazar '<' con '{'
        while ((pos = msg.find('<')) != std::string::npos) {
            msg.replace(pos, 1, "{");
        }
        Serial.println(msg.c_str());
    }
}

// Tarea principal para monitoreo meteorológico
void weatherTask(void *arg) {
    
    // Crear instancias de las clases para estación meteorológica y cálculos
    WeatherStation weatherStation;
    weatherMutex = xSemaphoreCreateMutex();
    
    // Habilitar depuración en RS485 si es necesario
    // setRS485Debug(true);
    
    while (1) {
        double gps_speed_kmh = traxGpsSpeedKmh();
        double gps_heading = traxGpsHeading();
        //double gps_speed_kmh = 0.0;
        //double gps_heading = 0.0;
        std::string gps_time_string = traxGpsTimeString();
        if (G_tolerate_no_gps && gps_speed_kmh < 0) {
            gps_speed_kmh = 0;
            gps_heading = 0;    
        }
        if (gps_speed_kmh < 0 || gps_heading < 0) {
            printForDebugWeather("WeatherTask: no gps data");
        } 
        else if (!weatherStation.getData()) {
            printForDebugWeather("WeatherTask: no climate data");
        } 
        else {
            WeatherData data;       // TODO: revisar este AI slop, condiciones que no se chequean
            data.valid = true;
            data.gpsTime = gps_time_string;
            data.gpsSpeedKmh = gps_speed_kmh;
            data.gpsHeading = gps_heading;
            data.windSpeed = weatherStation.getWindSpeed();
            data.pressure = weatherStation.getPressure();
            data.airTemp = weatherStation.getAirTemp();
            data.relativeHumidity = weatherStation.getRelativeMoisture();
            data.windDirection = weatherStation.getWindDirection();
            data.compass = weatherStation.getCompass();
            data.precipitation = weatherStation.getPrecipitation();

            if (xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                weatherState = data;
                weatherSeq++;
                xSemaphoreGive(weatherMutex);
            }

            printForDebugWeather("WeatherTask: raw data updated");
        }

        // Esperar antes del próximo ciclo
        delay(2000);
    }
}

bool weatherGetData(WeatherData& out, uint32_t& seq) {
    if (weatherMutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    out = weatherState;
    seq = weatherSeq;
    xSemaphoreGive(weatherMutex);
    return seq != 0;
}
