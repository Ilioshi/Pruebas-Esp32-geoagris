#ifndef WIFITASK_H
#define WIFITASK_H

#include <Arduino.h>

// ============================================================================
// PUENTE BIDIRECCIONAL TRAX (Serial) <-> WiFi/UDP
// ============================================================================
// Esta tarea crea un puente transparente que:
// - Reenvía mensajes del TRAX Serial al servidor UDP
// - Reenvía mensajes UDP entrantes al TRAX Serial
// - Maneja reconexión automática de WiFi
// ============================================================================

// Tarea principal WiFi - Puente TRAX <-> UDP
void wifiTask(void *pvParameters);

#endif