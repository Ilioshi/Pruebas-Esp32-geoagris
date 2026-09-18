#include "AgxWiFi.h"
#include "trax_utils.h"


// Configuración de timeouts
#define WIFI_CONNECT_TIMEOUT 20000      // 20 segundos para conectar
#define WIFI_RECONNECT_INTERVAL 20000   // Intentar reconectar cada 20s
#define WIFI_CHECK_INTERVAL 20000        // Verificar conexión cada 20s

// Configuración UDP
#define UDP_SERVER "s.agriexplorer.net"    // Servidor UDP destino
#define UDP_PORT 8888                        // Puerto UDP
#define UDP_LOCAL_PORT 8888                  // Puerto local para recibir UDP

// Configuración Access Point
#define AP_DEFAULT_IP IPAddress(192, 168, 4, 1)
#define AP_GATEWAY IPAddress(192, 168, 4, 1)
#define AP_SUBNET IPAddress(255, 255, 255, 0)

#define MAX_ATTEMPTS 3
#define ATTEMPT_TIMEOUT 1000 // 1 segundo por intento

// Configuración del puente
#define BRIDGE_BUFFER_SIZE 512          // Tamaño del buffer para mensajes


AgxWiFi* AgxWiFi::instance = nullptr;

AgxWiFi::AgxWiFi() : wifiConnected(false), udpReady(false), initialized(false)
{}

AgxWiFi& AgxWiFi::getInstance() {
    if (instance == nullptr) {
        instance = new AgxWiFi();
    }
    return *instance;
}

// ============================================================================
// WIFI
// ============================================================================

void AgxWiFi::begin() {
    if(!initialized) {
        initialized = true;
        // Modo STA normal
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
    }
}

/**
 * @brief Intenta conectar a una red WiFi utilizando las credenciales obtenidas del TRAX.
 * 
 * @note Escanea las redes disponibles y solo intenta conectar a aquellas que estén presentes.
 * Si la conexión es exitosa, inicializa el cliente UDP.
 */
bool AgxWiFi::connectToWiFi() {
    WifiCredentials credentials[MAX_WIFI_CREDENTIALS];
    bool connected = false;
    
    // Obtener credenciales desde TRAX
    int count = traxGetWifiCredentialsCount(credentials);

    if(count > 0) {
        // Escanear redes WiFi disponibles
        #if DEBUG_WIFI
        printDebugWifi("Escaneando redes WiFi...");
        #endif
        WiFi.disconnect();
        int networksFound = WiFi.scanNetworks();
        
        if (networksFound > 0) {
            #if DEBUG_WIFI
                printDebugWifi("Redes encontradas: " + std::to_string(networksFound));
            #endif

            // Intentar conectar solo con redes disponibles
            for (int i = 0; i < count && !connected; i++) {
                // Verificar si esta red está disponible
                bool networkAvailable = false;
                
                for (int j = 0; j < networksFound; j++) {
                    std::string ssid_found = WiFi.SSID(j).c_str();
                    if (ssid_found == credentials[i].ssid) {
                        networkAvailable = true;
                        break;
                    }
                }
                
                if (networkAvailable) {
                    #if DEBUG_WIFI
                        printDebugWifi("Conectando a: " + credentials[i].ssid);
                    #endif
                    WiFi.begin(credentials[i].ssid.c_str(), credentials[i].password.c_str());
                    WiFi.waitForConnectResult(WIFI_CONNECT_TIMEOUT);
                    
                    if (WiFi.isConnected()) {
                        
                        if(beginUDP(UDP_LOCAL_PORT)) {
                            udpReady = true;
                            #if DEBUG_WIFI
                                printDebugWifi("UDP inicializado");
                            #endif
                            if(pingServer()) {
                                #if DEBUG_WIFI
                                    printDebugWifi("Conexión establecida con" + credentials[i].ssid);
                                #endif
                                connected = true;
                                wifiConnected = true;
                            } else {
                                #if DEBUG_WIFI
                                    printDebugWifi("No se pudo comunicar con el servidor UDP.");
                                #endif
                                WiFi.disconnect();
                                udp.stop();
                                udpReady = false;
                            }
                        } else {
                            #if DEBUG_WIFI
                                printDebugWifi("Error al iniciar UDP");
                            #endif
                            WiFi.disconnect();
                        }
                    }
                } 
                #if DEBUG_WIFI
                    else {
                        printDebugWifi("Red no disponible: " + credentials[i].ssid);
                    }
                #endif
            }
            
            WiFi.scanDelete();
        } 
        #if DEBUG_WIFI
        else {
            printDebugWifi("No se encontraron redes WiFi.");
        }
        #endif
    } 
    #if DEBUG_WIFI
    else {
        printDebugWifi("No hay credenciales WiFi disponibles.");
    }
    #endif

    if (!connected) {
        #if DEBUG_WIFI
            printDebugWifi("No se pudo conectar a ninguna red WiFi.");
        #endif
        WiFi.disconnect();
        wifiConnected = false;
    }

    return connected;
}

/**
 * @brief Verifica el estado de la conexión WiFi y el timeout de actividad UDP.
 * 
 * @note Considera que el timeout ocurre si no se recibe actividad UDP dentro del intervalo `WIFI_CHECK_INTERVAL`.
 * 
 * @return `TRUE` Si la conexión está activa y no ha habido timeout.
 * @return `FALSE` Si la conexión está perdida o hubo timeout.
 */
bool AgxWiFi::checkConnectionStatus(unsigned long& lastReceivedSerial, bool& timeOutCheck) {

    bool isConnected; 

    if(timeOutCheck) {
        isConnected = (millis() - lastReceivedSerial) < WIFI_CHECK_INTERVAL;
        #if DEBUG_WIFI
        if(!isConnected)
            printDebugWifi("Timeout de actividad UDP");
        #endif
    } else {
        isConnected = WiFi.isConnected();       // Evitamos llamadas frecuentes a isConnected()
        #if DEBUG_WIFI
        if(!isConnected)
            printDebugWifi("WiFi desconectado");
        #endif
    }

    return isConnected;
}

bool AgxWiFi::isConnected() {
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    return wifiConnected;
}

void AgxWiFi::disconnect() {
    WiFi.disconnect();
    wifiConnected = false;
}

// ============================================================================
//                                  UDP
// ============================================================================

bool AgxWiFi::sendUDP(const uint8_t* data, size_t length) {
    bool success = false;
    if (!udpReady) {
        #if DEBUG_WIFI
            printDebugWifi("UDP no inicializado o servidor no configurado");
        #endif
        success = false;
    }
    
    udp.beginPacket(UDP_SERVER, UDP_PORT);
    udp.write(data, length);
    success = udp.endPacket();
    return success;
}

int AgxWiFi::receiveUDP(uint8_t* buffer, size_t maxLength) {
    int len = 0;
    if (udpReady) {
        int packetSize = udp.parsePacket();
        if (packetSize > 0) 
            len = udp.read(buffer, maxLength);
    }
    return len;
}

bool AgxWiFi::pingServer() {
    const char* pingMsg = ">RGP010170000000+0000000+00000000000000;ID=6240148;#SDM:77AC;*11<";
    bool success = false;
    
    #if DEBUG_WIFI
        printDebugWifi("Enviando ping UDP al servidor...");
    #endif

    if (udpReady)
        udp.beginPacket(UDP_SERVER, UDP_PORT);
    else
        return success;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS && !success; attempt++) {
        
        // Enviar ping
        if (sendUDP((const uint8_t*)pingMsg, strlen(pingMsg))) {
            // Esperar respuesta
            unsigned long start = millis();
            while (millis() - start < ATTEMPT_TIMEOUT && !success) {
                uint8_t buffer[BRIDGE_BUFFER_SIZE];
                int len = receiveUDP(buffer, sizeof(buffer) - 1);
                if (len > 0) {
                    buffer[len] = 0;
                    success = true;
                    
                    #if DEBUG_WIFI
                        std::string respuesta((char*)buffer, len);
                        printDebugWifi("Respuesta del servidor: " + respuesta);
                    #endif
                }
                vTaskDelay(pdMS_TO_TICKS(100));     // Pequeña espera antes de verificar nuevamente
            }
        }
        
        if (!success && attempt < MAX_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(200)); // Pequeña pausa entre intentos
        }
    }

    #if DEBUG_WIFI
    if(!success) {
        printDebugWifi("No hubo respuesta al ping UDP");
    }
    #endif

    return success;
}

// ============================================================================
//                              METODOS PRIVADOS
// ============================================================================

bool AgxWiFi::beginUDP(uint16_t localPort) {    
    udp.stop();
    if (udp.begin(localPort))
        udpReady = true;
    else
        udpReady = false;
    return udpReady;
}


#if DEBUG_WIFI
void AgxWiFi::printDebugWifi(const std::string& message) {
        Serial.print("[AgxWiFi] ");
        Serial.println(message.c_str());
}
#endif