#include <Arduino.h>
#include "BleSerialTask.h"
#include "BeaconTask.h"
#include "WeatherTask.h"
#include "CAN/CAN.h"
#include "CAN/CAN_sniffer.h"
#include "PressureTask.h"
#include "Pulse.h"
#include "WiFiTask.h"
#include "SendTask.h"
#include "Messanger_queue.h"
#include "trax_utils.h"

// TODO: implementar if de compilacion

std::string G_version = "2.0.3";

// Habilitacion de tareas
bool G_weatherTask = true;
bool G_beaconTask = false;
bool G_cantask = true;
bool G_pressureTask = false;

// Handles de tareas
#ifdef ENABLE_BLE
    TaskHandle_t G_bleSerialTaskHandle = NULL;
#endif

#ifdef ENABLE_BEACON
    TaskHandle_t G_beaconTaskHandle = NULL;
#endif

TaskHandle_t G_weatherTaskHandle = NULL;
TaskHandle_t G_pressureTaskHandle = NULL;

#ifdef ENABLE_WIFI
    TaskHandle_t G_wifiTaskHandle = NULL;
#endif

#ifdef ENABLE_CAN_SNIFFER
    TaskHandle_t G_canSnifferTaskHandle = NULL;
#else
    TaskHandle_t G_canReadTaskHandle = NULL;
#endif

void setup()
{   
    // TODO: emprolijar estos llamados, deberian estar en las tareas respectivas
    AgxSerial::getInstance().begin();
    traxInitialize();
    AgxBle::getInstance().begin(G_version);
    MessangerQueue_init();

    #ifdef ENABLE_PULSE
        initPulseCounters();
    #endif

    // CORE 0: Comunicaciones + sensores de baja frecuencia
    #ifdef ENABLE_BLE
        xTaskCreatePinnedToCore(bleSerialTask, "bleTask", 12288, NULL, 3, &G_bleSerialTaskHandle, 0);  // 12KB
    #endif

    #ifdef ENABLE_BEACON
        xTaskCreatePinnedToCore(beaconTask, "beaconTask", 12288, NULL, 1, &G_beaconTaskHandle, 0);  // 12KB
    #endif
    
    if (G_weatherTask) {
        xTaskCreatePinnedToCore(weatherTask, "weatherTask", 6144, NULL, 1, &G_weatherTaskHandle, 0);
    }
    #ifdef ENABLE_WIFI
        xTaskCreatePinnedToCore(wifiTask, "wifiTask", 8192, NULL, 2, &G_wifiTaskHandle, 0);  // 8KB
    #endif
    // SendTask centraliza envío
    xTaskCreatePinnedToCore(sendTask, "sendTask", 8192, NULL, 2, NULL, 0);
    
    // CORE 1: CAN crítico + sensores de alta frecuencia
    #ifdef ENABLE_CAN_SNIFFER
        xTaskCreatePinnedToCore(canSnifferTask, "canSnifferTask", 4096, NULL, 1, &G_canSnifferTaskHandle, 1);  // 4KB
    #else
    if (G_cantask) {
        xTaskCreatePinnedToCore(canReadTask, "canReadTask", 4096, NULL, 2, &G_canReadTaskHandle, 1);
    }
    #endif
    if (G_pressureTask) {
        xTaskCreatePinnedToCore(pressureTask, "pressureTask", 4096, NULL, 1, &G_pressureTaskHandle, 1);
    }
}

void loop()
{
    // No hacer nada en el loop principal
}
