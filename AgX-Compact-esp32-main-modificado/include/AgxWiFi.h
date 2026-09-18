#ifndef AGXWIFI_H
#define AGXWIFI_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ============================================================================
// CLASE PARA MANEJO SIMPLE DE WIFI Y UDP
// ============================================================================

class AgxWiFi {
public:
    static AgxWiFi& getInstance();
    
    // WiFi
    void begin();
    bool connectToWiFi();
    bool checkConnectionStatus(unsigned long& lastReceivedSerial, bool& timeOutCheck);
    bool isConnected();
    void disconnect();
    
    // UDP
    bool sendUDP(const uint8_t* data, size_t length);
    int receiveUDP(uint8_t* buffer, size_t maxLength);
    bool pingServer();
    
    private:
    AgxWiFi();
    
    static AgxWiFi* instance;
    WiFiUDP udp;
    bool wifiConnected;
    bool udpReady;
    bool initialized;
      
    bool beginUDP(uint16_t localPort);
    
    #if DEBUG_WIFI
    void printDebugWifi(const std::string& message);
    #endif
};

#endif