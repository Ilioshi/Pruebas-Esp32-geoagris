#ifdef ENABLE_BEACON

#include "BeaconTask.h"
#include "AgxBle.h"
#include "Beacon.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Configuracion
#define BEACON_SCAN_INTERVAL 3000
#define BEACON_REPORT_INTERVAL 60000
#define MAX_BEACONS_TO_REPORT 5
#define BEACON_RSSI_THRESHOLD -100      // Cuando se hagan pruebas en campo, ajustar este valor

// TODO: -Optimizar el uso de memoria dinamica en esta tarea. 
// BUG: Los traceback sugieren que el heap esta fragmentado para hacer nuevas asignaciones
// BUG: Problemas de memoria, saltan tracebacks cuando se intenta de conectar por BLE cuando la tarea de beacon esta activa

#ifndef ENABLE_BEACON_DEBUG
    #define ENABLE_BEACON_DEBUG 0
#endif

static BeaconAccumulator G_beaconAccumulator;       // TODO: ver de de convertirla a local
static unsigned long G_lastReportTime = 0;
static SemaphoreHandle_t beaconMutex = NULL;
static BeaconReportData beaconState;
static uint32_t beaconSeq = 0;

/*********************************************************************
*                          Local prototypes                          *
**********************************************************************/
static void printDebugBeaconTask(const std::string& message);

/*********************************************************************
*                          Public functions                          *
**********************************************************************/

void beaconTask(void* parameter) {
    unsigned long lastScanTime = 0;
    AgxBle& ble = AgxBle::getInstance();
    
    while(!ble.isInitialized()) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    ble.setUpScan();
    beaconMutex = xSemaphoreCreateMutex();

    printDebugBeaconTask("Iniciado");

    while (1) {
        unsigned long currentTime = millis();

        if(ble.busy()) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Escanear
        if((currentTime - lastScanTime) >= BEACON_SCAN_INTERVAL) {
            lastScanTime = currentTime;
            
            BLEScanResults* results = ble.scan();
            if(results != nullptr) {
                G_beaconAccumulator.processScan(results, BEACON_RSSI_THRESHOLD);
                printDebugBeaconTask("Scan #" + std::to_string(G_beaconAccumulator.scanCount()));
            }
        }
        
        // Reportar
        if ((currentTime - G_lastReportTime) >= BEACON_REPORT_INTERVAL) {
            G_lastReportTime = currentTime;
            auto nearest = G_beaconAccumulator.getNearestWithDistance(MAX_BEACONS_TO_REPORT);
            if (!nearest.empty()) {
                BeaconReportData data;
                data.nearest.reserve(nearest.size());
                for (const auto& entry : nearest) {
                    BeaconNearestEntry b;
                    b.mac = entry.first.c_str();
                    b.distance = entry.second;
                    data.nearest.push_back(b);
                }
                if (xSemaphoreTake(beaconMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    beaconState = data;
                    beaconSeq++;
                    xSemaphoreGive(beaconMutex);
                }
                printDebugBeaconTask("Reporte actualizado");
            }
            G_beaconAccumulator.clear();
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

bool beaconGetData(BeaconReportData& out, uint32_t& seq) {
    if (beaconMutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(beaconMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    out = beaconState;
    seq = beaconSeq;
    xSemaphoreGive(beaconMutex);
    return seq != 0;
}

/*********************************************************************
*                           Local functions                          *
**********************************************************************/
/**
 * @brief Imprime un mensaje de depuración relacionado con BeaconTask si el modo debug está activado.
 * 
 * @param message Mensaje a imprimir.
 */
static void printDebugBeaconTask(const std::string& message) {
    #if ENABLE_BEACON_DEBUG
        Serial.print("[BeaconTask] ");
        Serial.println(message.c_str());
    #endif // ENABLE_BEACON_DEBUG
}


#endif // ENABLE_BEACON