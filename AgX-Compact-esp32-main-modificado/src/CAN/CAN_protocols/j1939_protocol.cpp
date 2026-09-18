#include "CAN/CAN_protocols/j1939_protocol.h"
#include "CAN/CAN_protocols/Protocol_utils.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "DutCanDiagnostic.h"

/**********************************************************************
*                             Defines                                 *
***********************************************************************/

#ifndef J1939_DEBUG_ERRORS
  #define J1939_DEBUG_ERRORS 0
#endif

/**********************************************************************
*                             Structs                                 *
***********************************************************************/

typedef struct {
    uint16_t rpm;
    uint8_t torque;
    float fuelConsumption;
    uint8_t engineTemperature;
    uint32_t engineHours;
    unsigned long lastRPMDataTime;
    unsigned long lastTorqueDataTime;
    unsigned long lastFuelConsumptionDataTime;
    unsigned long lastEngineTempDataTime;
    unsigned long lastEngineHoursDataTime;
} EngineData;

/**********************************************************************
*                          Global Variables                           *
***********************************************************************/

// Mutex para proteger la estructura compartida
SemaphoreHandle_t j1939DataMutex = NULL;

static EngineData engineData;
static uint32_t j1939Seq = 0;

/***********************************************************************************************
*                                 Local Functions Prototypes                                   *
************************************************************************************************/

static void printForDebugJ1939(const char* message);
static void j1939parseRPMAndTorqueData(const uint8_t* data, uint8_t length);
static void j1939parseFuelConsumptionData(const uint8_t* data, uint8_t length);
static void j1939parseEngineTempData(const uint8_t* data, uint8_t length);
static void j1939parseEngineHoursData(const uint8_t* data, uint8_t length);

/***********************************************************************************************
*                                       Public Functions                                       *
************************************************************************************************/

void j1939ProcessMessage(long unsigned int rxId, unsigned char *rxBuf, unsigned char len) {
    // J1939 uses extended DATA frames. Never decode short/RTR/error frames.
    if ((rxId & 0xE0000000UL) != 0x80000000UL || len < 8 || rxBuf == nullptr ||
        j1939DataMutex == nullptr) return;
    unsigned long pgn = EXTRACT_PGN(rxId);

    if (xSemaphoreTake(j1939DataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    switch (pgn) {
        case 0xF004: // RPM y Torque
            printForDebugJ1939("RPM & Torque PGN");
            j1939parseRPMAndTorqueData(rxBuf, len);
            dutCanDecoded(pgn);
            break;
        case 0xFEF2: // Consumo de Combustible
            printForDebugJ1939("Fuel Consumption PGN");
            j1939parseFuelConsumptionData(rxBuf, len);
            dutCanDecoded(pgn);
            break;
        case 0xFEEE: // Temperatura del Motor
            printForDebugJ1939("Engine Temperature PGN");
            j1939parseEngineTempData(rxBuf, len);
            dutCanDecoded(pgn);
            break;
        case 0xFEE5: // Horas del Motor
            printForDebugJ1939("Engine Hours PGN");
            j1939parseEngineHoursData(rxBuf, len);
            dutCanDecoded(pgn);
            break;
    }

    xSemaphoreGive(j1939DataMutex);
}


bool j1939GetData(J1939Data& out, uint32_t& seq) {
    if (j1939DataMutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(j1939DataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }

    out.hasRpm = (engineData.lastRPMDataTime != 0);
    out.rpm = engineData.rpm;
    out.rpmTs = engineData.lastRPMDataTime;

    out.hasTorque = (engineData.lastTorqueDataTime != 0);
    out.torque = engineData.torque;
    out.torqueTs = engineData.lastTorqueDataTime;

    out.hasFuelConsumption = (engineData.lastFuelConsumptionDataTime != 0);
    out.fuelConsumption = engineData.fuelConsumption;
    out.fuelConsumptionTs = engineData.lastFuelConsumptionDataTime;

    out.hasEngineTemperature = (engineData.lastEngineTempDataTime != 0);
    out.engineTemperature = engineData.engineTemperature;
    out.engineTemperatureTs = engineData.lastEngineTempDataTime;

    out.hasEngineHours = (engineData.lastEngineHoursDataTime != 0);
    out.engineHours = engineData.engineHours;
    out.engineHoursTs = engineData.lastEngineHoursDataTime;

    seq = j1939Seq;

    xSemaphoreGive(j1939DataMutex);
    return seq != 0;
}

/***********************************************************************************************
*                                 Local Functions Definitions                                  *
************************************************************************************************/

static void printForDebugJ1939(const char* message) {
    #if J1939_DEBUG_ERRORS
        Serial.printf("[J1939] %s\n", message);
    #endif
}

static void j1939parseRPMAndTorqueData(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        engineData.lastRPMDataTime = millis();
        engineData.rpm = ((data[4] << 8) | data[3]) / 8;
        engineData.lastTorqueDataTime = millis();
        engineData.torque = data[2] - 125;
        j1939Seq++;
    }
}

static void j1939parseFuelConsumptionData(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        engineData.lastFuelConsumptionDataTime = millis();
        engineData.fuelConsumption = ((data[1] << 8) | data[0]) / 20.0;
        j1939Seq++;
    }
}

static void j1939parseEngineTempData(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        engineData.lastEngineTempDataTime = millis();
        engineData.engineTemperature = data[0] - 40;
        j1939Seq++;
    }
}

static void j1939parseEngineHoursData(const uint8_t* data, uint8_t length) {
    if (length >= 8) {
        engineData.lastEngineHoursDataTime = millis();
        engineData.engineHours = ((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]) / 20;
        j1939Seq++;
    }
}
