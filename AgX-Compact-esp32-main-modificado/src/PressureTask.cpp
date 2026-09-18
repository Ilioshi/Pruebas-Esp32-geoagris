#include "PressureTask.h"
#include "PressureSensor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Variable para depuración
static bool G_debug_pressure = false;

// Período de lectura del sensor (en milisegundos)
static const unsigned long PRESSURE_READ_INTERVAL = 3000; // 3 segundos

static SemaphoreHandle_t pressureMutex = NULL;
static PressureData pressureState;
static uint32_t pressureSeq = 0;

// Función para imprimir mensajes de depuración
static void printForDebugPressure(const std::string& message) {
    if (G_debug_pressure) {
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

// Tarea para monitorear el sensor de presión
void pressureTask(void* parameter) {
    // Crear instancia del sensor de presión
    PressureSensor pressureSensor;
    pressureMutex = xSemaphoreCreateMutex();
    
    // Para desarrollo/pruebas, descomentar para usar valores simulados
    // pressureSensor.setForceMode(true, 10.5);
    
    // Tiempo de la última lectura
    unsigned long lastReadTime = 0;
    
    // Mensaje de inicio
    printForDebugPressure(">STX02,e32:pressure_task_started<");
    
    // Bucle principal de la tarea
    while (1) {
        unsigned long currentTime = millis();
        
        // Verificar si es momento de leer el sensor
        if (currentTime - lastReadTime >= PRESSURE_READ_INTERVAL) {
            lastReadTime = currentTime;
            
            // Leer el sensor de presión
            if (pressureSensor.getData()) {
                // Obtener el valor de presión
                float pressure = pressureSensor.getPressure();
                
                PressureData data;
                data.valid = true;
                data.pressure = pressure;
                if (xSemaphoreTake(pressureMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    pressureState = data;
                    pressureSeq++;
                    xSemaphoreGive(pressureMutex);
                }

                // Depuración
                printForDebugPressure(std::string("Pressure: ") + std::to_string(pressure));
            } else {
                // Error al leer el sensor
                PressureData data;
                data.valid = false;
                if (xSemaphoreTake(pressureMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    pressureState = data;
                    pressureSeq++;
                    xSemaphoreGive(pressureMutex);
                }
                printForDebugPressure("Error reading pressure sensor");
            }
        }
        
        // Pequeño delay para evitar consumo excesivo de CPU
        delay(100);
    }
}

bool pressureGetData(PressureData& out, uint32_t& seq) {
    if (pressureMutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(pressureMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    out = pressureState;
    seq = pressureSeq;
    xSemaphoreGive(pressureMutex);
    return seq != 0;
}
