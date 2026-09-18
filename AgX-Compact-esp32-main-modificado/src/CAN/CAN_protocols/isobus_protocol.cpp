/**
 * @file isobus_protocol.cpp
 * @brief ISOBUS protocol implementation for CAN communication
 * @version 0.1
 * @date 02-02-2026
 * @protocol ISOBUS 11783
 * 
 */

#include "CAN/CAN_protocols/isobus_protocol.h"
#include "CAN/CAN_protocols/Protocol_utils.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**********************************************************************
*                             Defines                                 *
***********************************************************************/

#ifndef ISOBUS_DEBUG_ERRORS
  #define ISOBUS_DEBUG_ERRORS 0
#endif

/**********************************************************************
*                              Macros                                 *
***********************************************************************/



/**********************************************************************
*                             Structs                                 *
***********************************************************************/

// Estructura de datos ISOBUS
static struct ImplementData {
    uint32_t latitudeReal;
    uint32_t longitudeReal;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t month;
    uint8_t day;
    uint8_t year;
    uint16_t compass;
    uint16_t vehicleSpeed;
    uint16_t altitude;
    uint32_t setPointvolumen;
    uint32_t volumen;
    uint32_t Product0Solid;
    uint32_t Product1Solid;
    uint32_t product0SetSolid;
    uint32_t product1SetSolid;
    uint32_t Product0liquid;
    uint32_t Product1liquid;
    uint32_t product0SetLiquid;
    uint32_t product1SetLiquid;
    uint8_t WorkState;
    uint32_t pumpPressureP0;
    uint32_t pumpPressureP1;
    uint32_t statusSections;
#ifdef ENABLE_SIMULATION
    bool hasSimWindSpeed;
    bool hasSimWindDirection;
    bool hasSimHumidity;
    bool hasSimTemperature;
    double simWindSpeed;       // km/h
    double simWindDirection;   // deg
    double simHumidity;        // %
    double simTemperature;     // C
#endif
    unsigned long lastCoordinateTime;
    unsigned long lastDateTime;
    unsigned long lastspeeddirectionTime;
    unsigned long lastWorkState;
    unsigned long lastproduct0SetSolidTime;
    unsigned long lastproduct1SetSolidTime;
    unsigned long lastProduct0SolidTime;
    unsigned long lastProduct1SolidTime;
    unsigned long lastProduct0liquidTime;
    unsigned long lastProduct1liquidTime;
    unsigned long lastproduct0SetLiquidTime;
    unsigned long lastproduct1SetLiquidTime;
    unsigned long lastpumpPressureProduct0Time;
    unsigned long lastpumpPressureProduct1Time;
    unsigned long laststatusSectionsTime;
} implementData;

/**********************************************************************
*                          Global Variables                           *
***********************************************************************/

// Mutex para proteger la estructura compartida
SemaphoreHandle_t isobusDataMutex = NULL;

static uint32_t isobusSeq = 0;

/***********************************************************************************************
*                                 Local Functions Prototypes                                  *
************************************************************************************************/

static void printForDebugIsobus(const char* message);
static void isobusProcessDDI(const uint8_t* data, uint8_t length);
static void isobusInterpretPosition(const uint8_t* data, uint8_t length);
static void isobusInterpretDateTime(const uint8_t* data, uint8_t length);
static void isobusInterpretSpeedDirection(const uint8_t* data, uint8_t length);
static void isobusInterpretWorkState(const uint8_t* data, uint8_t length);
static void isobusInterpretPumpPressureProduct0(const uint8_t* data, uint8_t length);
static void isobusInterpretPumpPressureProduct1(const uint8_t* data, uint8_t length);
static void isobusInterpretStatusOfTheSections(const uint8_t* data, uint8_t length);
static void isobusInterpretSetSolidProduct0(const uint8_t* data, uint8_t length);
static void isobusInterpretSetSolidProduct1(const uint8_t* data, uint8_t length);
static void isobusInterpretSolidProduct0(const uint8_t* data, uint8_t length);
static void isobusInterpretSolidProduct1(const uint8_t* data, uint8_t length);
static void isobusInterpretliquidProduct0(const uint8_t* data, uint8_t length);
static void isobusInterpretliquidProduct1(const uint8_t* data, uint8_t length);
static void isobusInterpretSetliquidProduct0(const uint8_t* data, uint8_t length);
static void isobusInterpretSetliquidProduct1(const uint8_t* data, uint8_t length);

#ifdef ENABLE_SIMULATION
    static float readFloatLE(const uint8_t* data);
#endif

/***********************************************************************************************
*                                       Public Functions                                       *
************************************************************************************************/

void isobusProcessMessage(long unsigned int rxId, unsigned char *rxBuf, unsigned char len) {
    if (xSemaphoreTake(isobusDataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    // TODO: La mayoria a eccepción del ultimo no son ISOBUS, pasarlo a J1939
    switch (EXTRACT_PGN(rxId)) {
        case PGN_VEHICLE_POSITION_1:        // Posición
            printForDebugIsobus("Position PGN");
            isobusInterpretPosition(rxBuf, len);
            break;
        case PGN_DATE_TIME:                 // Fecha y Hora
            printForDebugIsobus("DateTime PGN");
            isobusInterpretDateTime(rxBuf, len);
            break;
        case PGN_SPEED_DIRECTION:           // Velocidad y Dirección
            printForDebugIsobus("Speed and Direction PGN");
            isobusInterpretSpeedDirection(rxBuf, len);
            break;
        case PGN_PROCESS_DATA_MESSAGE:      // Variables de Proceso
            printForDebugIsobus("Process Data PGN");
            isobusProcessDDI(rxBuf, len);
            break;
        default:
            printForDebugIsobus("Unknown PGN");
            break;
    }
    
    xSemaphoreGive(isobusDataMutex);
}


bool isobusGetData(IsobusData& out, uint32_t& seq) {
    if (isobusDataMutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(isobusDataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }

    out.hasPosition = (implementData.lastCoordinateTime != 0);
    out.latitude = implementData.latitudeReal;
    out.longitude = implementData.longitudeReal;
    out.positionTs = implementData.lastCoordinateTime;

    out.hasDateTime = (implementData.lastDateTime != 0);
    out.seconds = implementData.seconds;
    out.minutes = implementData.minutes;
    out.hours = implementData.hours;
    out.month = implementData.month;
    out.day = implementData.day;
    out.year = implementData.year;
    out.dateTimeTs = implementData.lastDateTime;

    out.hasSpeedDirection = (implementData.lastspeeddirectionTime != 0);
    out.compass = implementData.compass;
    out.vehicleSpeed = implementData.vehicleSpeed;
    out.altitude = implementData.altitude;
    out.speedDirectionTs = implementData.lastspeeddirectionTime;

    out.hasWorkState = (implementData.lastWorkState != 0);
    out.workState = implementData.WorkState ? 1U : 0U;
    out.workStateTs = implementData.lastWorkState;

    out.hasPumpPressureP0 = (implementData.lastpumpPressureProduct0Time != 0);
    out.pumpPressureP0 = implementData.pumpPressureP0;
    out.pumpPressureP0Ts = implementData.lastpumpPressureProduct0Time;

    out.hasPumpPressureP1 = (implementData.lastpumpPressureProduct1Time != 0);
    out.pumpPressureP1 = implementData.pumpPressureP1;
    out.pumpPressureP1Ts = implementData.lastpumpPressureProduct1Time;

    out.hasStatusSections = (implementData.laststatusSectionsTime != 0);
    out.statusSections = implementData.statusSections;
    out.statusSectionsTs = implementData.laststatusSectionsTime;

    out.hasSetSolidP0 = (implementData.lastproduct0SetSolidTime != 0);
    out.setSolidP0 = implementData.product0SetSolid;
    out.setSolidP0Ts = implementData.lastproduct0SetSolidTime;

    out.hasSetSolidP1 = (implementData.lastproduct1SetSolidTime != 0);
    out.setSolidP1 = implementData.product1SetSolid;
    out.setSolidP1Ts = implementData.lastproduct1SetSolidTime;

    out.hasSolidP0 = (implementData.lastProduct0SolidTime != 0);
    out.solidP0 = implementData.Product0Solid;
    out.solidP0Ts = implementData.lastProduct0SolidTime;

    out.hasSolidP1 = (implementData.lastProduct1SolidTime != 0);
    out.solidP1 = implementData.Product1Solid;
    out.solidP1Ts = implementData.lastProduct1SolidTime;

    out.hasSetLiquidP0 = (implementData.lastproduct0SetLiquidTime != 0);
    out.setLiquidP0 = implementData.product0SetLiquid;
    out.setLiquidP0Ts = implementData.lastproduct0SetLiquidTime;

    out.hasSetLiquidP1 = (implementData.lastproduct1SetLiquidTime != 0);
    out.setLiquidP1 = implementData.product1SetLiquid;
    out.setLiquidP1Ts = implementData.lastproduct1SetLiquidTime;

    out.hasLiquidP0 = (implementData.lastProduct0liquidTime != 0);
    out.liquidP0 = implementData.Product0liquid;
    out.liquidP0Ts = implementData.lastProduct0liquidTime;

    out.hasLiquidP1 = (implementData.lastProduct1liquidTime != 0);
    out.liquidP1 = implementData.Product1liquid;
    out.liquidP1Ts = implementData.lastProduct1liquidTime;

#ifdef ENABLE_SIMULATION
    out.hasSimWindSpeed = implementData.hasSimWindSpeed;
    out.simWindSpeed = implementData.simWindSpeed;

    out.hasSimWindDirection = implementData.hasSimWindDirection;
    out.simWindDirection = implementData.simWindDirection;

    out.hasSimHumidity = implementData.hasSimHumidity;
    out.simHumidity = implementData.simHumidity;

    out.hasSimTemperature = implementData.hasSimTemperature;
    out.simTemperature = implementData.simTemperature;
#endif

    seq = isobusSeq;
    xSemaphoreGive(isobusDataMutex);
    return seq != 0;
}

void isobusClearSimFlags() {
#ifdef ENABLE_SIMULATION
    if (isobusDataMutex == NULL) {
        return;
    }
    if (xSemaphoreTake(isobusDataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    implementData.hasSimWindSpeed = false;
    implementData.hasSimWindDirection = false;
    implementData.hasSimHumidity = false;
    implementData.hasSimTemperature = false;

    xSemaphoreGive(isobusDataMutex);
#endif
}

/***********************************************************************************************
*                                 Local Functions Definitions                                  *
************************************************************************************************/

static void printForDebugIsobus(const char* message) {
    #if ISOBUS_DEBUG_ERRORS
        Serial.printf("[ISOBUS] %s\n", message);
    #endif
}

static void isobusProcessDDI(const uint8_t* data, uint8_t length) {
    if (length < 4) 
        return;

    uint8_t prod = 0;
    if (data[0] != 0x00) {
        int8_t dec = ((int)data[0] - 3) / 16 - 1;
        if (dec >= 0 && dec <= 13)
            prod = (uint8_t)dec;
    }

    switch (READ_DDI(&data[2])) {
        case DDI_SETPOINT_VOLUME_PER_AREA: // Liquid setpoint
            if (prod == 1) isobusInterpretSetliquidProduct0(data, length);
            else if (prod == 2) isobusInterpretSetliquidProduct1(data, length);
            break;

        case DDI_ACTUAL_VOLUME_PER_AREA: // Liquid rate
            if (prod == 1) isobusInterpretliquidProduct0(data, length);
            else if (prod == 2) isobusInterpretliquidProduct1(data, length);
            break;

        case DDI_SETPOINT_MASS_PER_AREA: // Solid setpoint
            if (prod == 1) isobusInterpretSetSolidProduct0(data, length);
            else if (prod == 2) isobusInterpretSetSolidProduct1(data, length);
            break;

        case DDI_ACTUAL_MASS_PER_AREA: // Solid rate
            if (prod == 1) isobusInterpretSolidProduct0(data, length);
            else if (prod == 2) isobusInterpretSolidProduct1(data, length);
            break;

        case DDI_ACTUAL_WORK_STATE: // Work state
            isobusInterpretWorkState(data, length);
            break;

        case DDI_ACTUAL_PRODUCT_PRESSURE: // Pressure
            if (prod == 1) isobusInterpretPumpPressureProduct0(data, length);
            else if (prod == 2) isobusInterpretPumpPressureProduct1(data, length);
            break;

        case DDI_ACTUAL_CONDENSED_WORK_STATE: // Sections
            isobusInterpretStatusOfTheSections(data, length);
            break;

    #ifdef ENABLE_SIMULATION
        case DDI_HUMIDITY:          // Humidity
            if (length >= 6) {
                implementData.simHumidity = (double)(READ_U16_LE(&data[4]));
                implementData.hasSimHumidity = true;
                isobusSeq++;
            }
            break;
        case DDI_TEMPERATURE:       // Temperature
            if (length >= 8) {
                const float v = readFloatLE(&data[4]);
                implementData.simTemperature = (double)(v);
                implementData.hasSimTemperature = true;
                isobusSeq++;
            }
            break;
        case DDI_WIND_SPEED:        // Wind speed
            if (length >= 8) {
                const float v = readFloatLE(&data[4]);
                implementData.simWindSpeed = (double)(v);
                implementData.hasSimWindSpeed = true;
                isobusSeq++;
            }
            break;
        case DDI_WIND_DIRECTION:    // Wind direction
            if (length >= 6) {
                implementData.simWindDirection = (double)(READ_U16_LE(&data[4]));
                implementData.hasSimWindDirection = true;
                isobusSeq++;
            }
            break;
    #endif

        default:
            break;
    }
}

static void isobusInterpretPosition(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        implementData.lastCoordinateTime = millis();
        
        implementData.latitudeReal = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
        implementData.longitudeReal = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretDateTime(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        implementData.lastDateTime = millis();
        implementData.seconds = data[0];
        implementData.minutes = data[1];
        implementData.hours = data[2];
        implementData.month = data[3];
        implementData.day = data[4];
        implementData.year = data[5];
        isobusSeq++;
    }
}

static void isobusInterpretSpeedDirection(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        implementData.lastspeeddirectionTime = millis();

        uint16_t heading_raw = ((data[1] << 8) | data[0]);
        uint16_t speed_raw   = ((data[3] << 8) | data[2]);
        uint16_t alt_raw     = ((data[7] << 8) | data[6]);

        implementData.compass      = heading_raw;
        implementData.vehicleSpeed = speed_raw;
        implementData.altitude     = alt_raw;
        isobusSeq++;
    }
}

static void isobusInterpretWorkState(const uint8_t* data, uint8_t length){
    if (length >= 8) {
        implementData.lastWorkState = millis();

        uint32_t valor = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

        if (valor == 0) {
            implementData.WorkState = false;
        } else if (valor == 1) {
            implementData.WorkState = true;
        } else {
            printForDebugIsobus("Valor de modo selectivo desconocido");
        }
        isobusSeq++;
    }
}

static void isobusInterpretPumpPressureProduct0(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastpumpPressureProduct0Time = millis();
        implementData.pumpPressureP0 = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretPumpPressureProduct1(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastpumpPressureProduct1Time = millis();
        implementData.pumpPressureP1 = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretStatusOfTheSections(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.laststatusSectionsTime = millis();
        implementData.statusSections = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSetSolidProduct0(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastproduct0SetSolidTime = millis();
        implementData.product0SetSolid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSetSolidProduct1(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastproduct1SetSolidTime = millis();
        implementData.product1SetSolid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSolidProduct0(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastProduct0SolidTime = millis();
        implementData.Product0Solid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSolidProduct1(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastProduct1SolidTime = millis();
        implementData.Product1Solid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretliquidProduct0(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastProduct0liquidTime = millis();
        implementData.Product0liquid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretliquidProduct1(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastProduct1liquidTime = millis();
        implementData.Product1liquid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSetliquidProduct0(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastproduct0SetLiquidTime = millis();
        implementData.product0SetLiquid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

static void isobusInterpretSetliquidProduct1(const uint8_t* data, uint8_t length) {
    if (length >= 4) {
        implementData.lastproduct1SetLiquidTime = millis();
        implementData.product1SetLiquid = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        isobusSeq++;
    }
}

#ifdef ENABLE_SIMULATION        // TODO: es un asco este codigo, refactorizar después

static float readFloatLE(const uint8_t* data) {
  float v = 0.0f;
  memcpy(&v, data, sizeof(float));
  return v;
}

#endif