#ifndef ISOBUS_PROTOCOL_H
#define ISOBUS_PROTOCOL_H

#include <Arduino.h>
#include <freertos/semphr.h>

// Datos crudos ISOBUS entregados por canReadTask
struct IsobusData {
    bool hasPosition = false;
    uint32_t latitude = 0;
    uint32_t longitude = 0;
    uint32_t positionTs = 0;

    bool hasDateTime = false;
    uint8_t seconds = 0;
    uint8_t minutes = 0;
    uint8_t hours = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t year = 0;
    uint32_t dateTimeTs = 0;

    bool hasSpeedDirection = false;
    uint16_t compass = 0;
    uint16_t vehicleSpeed = 0;
    uint16_t altitude = 0;
    uint32_t speedDirectionTs = 0;

    bool hasWorkState = false;
    uint8_t workState = 0;
    uint32_t workStateTs = 0;

    bool hasPumpPressureP0 = false;
    uint32_t pumpPressureP0 = 0;
    uint32_t pumpPressureP0Ts = 0;

    bool hasPumpPressureP1 = false;
    uint32_t pumpPressureP1 = 0;
    uint32_t pumpPressureP1Ts = 0;

    bool hasStatusSections = false;
    uint32_t statusSections = 0;
    uint32_t statusSectionsTs = 0;

    bool hasSetSolidP0 = false;
    uint32_t setSolidP0 = 0;
    uint32_t setSolidP0Ts = 0;

    bool hasSetSolidP1 = false;
    uint32_t setSolidP1 = 0;
    uint32_t setSolidP1Ts = 0;

    bool hasSolidP0 = false;
    uint32_t solidP0 = 0;
    uint32_t solidP0Ts = 0;

    bool hasSolidP1 = false;
    uint32_t solidP1 = 0;
    uint32_t solidP1Ts = 0;

    bool hasSetLiquidP0 = false;
    uint32_t setLiquidP0 = 0;
    uint32_t setLiquidP0Ts = 0;

    bool hasSetLiquidP1 = false;
    uint32_t setLiquidP1 = 0;
    uint32_t setLiquidP1Ts = 0;

    bool hasLiquidP0 = false;
    uint32_t liquidP0 = 0;
    uint32_t liquidP0Ts = 0;

    bool hasLiquidP1 = false;
    uint32_t liquidP1 = 0;
    uint32_t liquidP1Ts = 0;

#ifdef ENABLE_SIMULATION
    bool hasSimWindSpeed = false;
    double simWindSpeed = 0;

    bool hasSimWindDirection = false;
    double simWindDirection = 0;

    bool hasSimHumidity = false;
    double simHumidity = 0;

    bool hasSimTemperature = false;
    double simTemperature = 0;
    
#endif
};

// Inicializa el módulo ISOBUS (mutex y estructuras de datos)
extern SemaphoreHandle_t isobusDataMutex;

// Procesa mensajes CAN entrantes para ISOBUS
void isobusProcessMessage(long unsigned int rxId, unsigned char *rxBuf, unsigned char len);

// Devuelve datos parseados si hay cambios
bool isobusGetData(IsobusData& out, uint32_t& seq);

// Limpia flags de simulación (usado tras envío exitoso)
void isobusClearSimFlags();

#endif // ISOBUS_PROTOCOL_H
