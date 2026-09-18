#ifdef ENABLE_WIFI

#include "WiFiTask.h"
#include "trax_utils.h"
#include "AgxSerial.h"
#include "AgxBle.h"
#include "AgxWiFi.h"
#include <freertos/FreeRTOS.h>

// ============================================================================
// CONFIGURACIÓN WIFI
// ============================================================================

#define BLE_CONNECTION_TIMEOUT      60000   // 60 segundos para esperar desconexión de BLE
#define WIFI_RECONNECT_INTERVAL     20000   // Intentar reconectar cada 20s

// Configuración del puente
#define BRIDGE_BUFFER_SIZE          512          // Tamaño del buffer para mensajes

static AgxSerial& G_serial = AgxSerial::getInstance();

// ============================================================================
// PROTOTIPOS DE FUNCIONES LOCALES
// ============================================================================

static void printDebugWifiTask(const std::string& message);
static void serialToUdp(AgxWiFi& wifi, unsigned long& lastReceivedSerial, bool& timeOutCheck);
static void udpToSerial(AgxWiFi& wifi, unsigned long& lastReceivedSerial, bool& timeOutCheck);

// ============================================================================
// TAREA PRINCIPAL - PUENTE WIFI <-> SERIAL
// ============================================================================

void wifiTask(void *pvParameters) {
    AgxBle& ble = AgxBle::getInstance();
    AgxWiFi& wifi = AgxWiFi::getInstance();
    
    wifi.begin();
    printDebugWifiTask("Tarea inicializada");
    wifi.connectToWiFi();
    
    static unsigned long lastReceivedSerial = 0;
    bool timeOutCheck = false;
    
    while(1) {
        if (ble.connected()) {
            timeOutCheck = false;
            traxSetIP0();
            // Si BLE está conectado, no hacer puente
            vTaskDelay(pdMS_TO_TICKS(BLE_CONNECTION_TIMEOUT));
        }
        else if(wifi.checkConnectionStatus(lastReceivedSerial, timeOutCheck)) {
            traxSetTR1();
            serialToUdp(wifi, lastReceivedSerial, timeOutCheck);
            vTaskDelay(pdMS_TO_TICKS(100));   // Para darle tiempo a recibir respuesta
            udpToSerial(wifi, lastReceivedSerial, timeOutCheck);
        } else {
            timeOutCheck = false;
            if(!wifi.connectToWiFi()) {
                traxSetIP0();
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL)); // Esperar antes de reintentar
            }
        }
    }
}

// ============================================================================
// DEFINICIONES DE FUNCIONES LOCALES
// ============================================================================


/**
 * @brief Imprime un mensaje de depuración relacionado con WiFi si el modo debug está activado.
 * 
 * @param message Mensaje a imprimir.
 */
static void printDebugWifiTask(const std::string& message) {
    #if DEBUG_WIFI
        Serial.print("[WiFiTask] ");
        Serial.println(message.c_str());
    #endif
}

/**
 * @brief Reenvía datos desde el puerto serial al servidor UDP.
 * 
 */
static void serialToUdp(AgxWiFi& wifi, unsigned long& lastReceivedSerial, bool& timeOutCheck) {
    if (G_serial.available() > 0) {
        
        // Leer todos los bytes disponibles
        size_t bytesAvailable = G_serial.available();
        uint8_t buffer[BRIDGE_BUFFER_SIZE];
        size_t bytesRead = G_serial.read(buffer, min(bytesAvailable,
                                            (size_t)BRIDGE_BUFFER_SIZE));
        
        if (bytesRead > 0) {
            std::string mensaje((char*)buffer, bytesRead);

            printDebugWifiTask("Serial a UDP: " + std::to_string(bytesRead) + " bytes");
            printDebugWifiTask("Contenido: " + mensaje);

            // Enviar directamente por UDP
            if((mensaje.find("#SDM") != std::string::npos || 
                mensaje.find("#LOG") != std::string::npos)){
                
                bool success = wifi.sendUDP(buffer, bytesRead);
                
                if (!success) 
                    printDebugWifiTask("Error al enviar UDP");

                if(!timeOutCheck) {
                    lastReceivedSerial = millis();
                    timeOutCheck = true;
                    printDebugWifiTask("Timeout check activado (#SDM o #LOG detectado)");
                }
            }
        }
    }
}

/**
 * @brief Reenvía datos desde el servidor UDP al puerto serial.
 * 
 */
static void udpToSerial(AgxWiFi& wifi, unsigned long& lastReceivedSerial, bool& timeOutCheck) {
    uint8_t buffer[BRIDGE_BUFFER_SIZE];
    int bytesRead = wifi.receiveUDP(buffer, BRIDGE_BUFFER_SIZE);
    
    if (bytesRead > 0) {
        // Actualizar timestamp de actividad WiFi/UDP
        lastReceivedSerial = millis();
        
        std::string mensaje((char*)buffer, bytesRead);
        
        timeOutCheck = false;
        printDebugWifiTask("UDP a Serial: " + std::to_string(bytesRead) + " bytes");
        printDebugWifiTask("Contenido: " + mensaje);

        // Enviar directamente a Serial
        G_serial.write(buffer, bytesRead);
    }
}

#endif // ENABLE_WIFI