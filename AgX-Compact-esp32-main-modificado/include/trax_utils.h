#pragma once
#include <Arduino.h>
#include <string>
#include "agx_utils.h"

#define MAX_WIFI_CREDENTIALS 10

struct WifiCredentials {
    std::string ssid;
    std::string password;
};

std::string traxSendReceive(const std::string& data);
void traxInitialize(); // Call before starting concurrent tasks.
double traxGpsSpeedKmh(void); //devuelve -1 si no logra obtener datos de GPS
double traxGpsHeading(void); //devuelve -1 si no logra obtener datos de GPS
std::string traxGpsTimeString(void); //devuelve "010170000000"" si no logra obtener datos de GPS
std::string traxGetMachineName(void); //devuelve "unknown" si no logra obtener el nombre de la máquina
int traxGetWifiCredentialsCount(WifiCredentials* arr);  // Devuelve cuántas credenciales hay registradas
void traxSetIP0(); // Configura TRAX para usar IP0 en conexiones salientes
void traxSetTR1(); // Configura TRAX para usar TR1 en conexiones salientes
