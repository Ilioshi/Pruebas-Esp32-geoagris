#include "SendTaskCan.h"
#include "CAN/CAN_protocols/CAN_utils.h"
#include <cstdio>

static bool isFresh(uint32_t ts, uint32_t now, unsigned long timeoutMs) {
    return ts != 0 && (now - ts) <= timeoutMs;
}

bool SendTaskCanJ1939::buildMessage(const J1939Data& data, uint32_t nowMs, std::string& outMessage) {
    const unsigned long timeoutRpm = 5000;
    const unsigned long timeoutTorque = 5000;
    const unsigned long timeoutFuel = 5000;
    const unsigned long timeoutTemp = 5000;
    const unsigned long timeoutHours = 3600000;

    const bool rpmValid = data.hasRpm && isFresh(data.rpmTs, nowMs, timeoutRpm);
    const bool torqueValid = data.hasTorque && isFresh(data.torqueTs, nowMs, timeoutTorque);
    const bool fuelValid = data.hasFuelConsumption && isFresh(data.fuelConsumptionTs, nowMs, timeoutFuel);
    const bool tempValid = data.hasEngineTemperature && isFresh(data.engineTemperatureTs, nowMs, timeoutTemp);
    const bool hoursValid = data.hasEngineHours && isFresh(data.engineHoursTs, nowMs, timeoutHours);

    if (!(rpmValid || torqueValid || fuelValid || tempValid || hoursValid)) {
        return false;
    }

    char rpmStr[6];
    char torqueStr[5];
    char fuelStr[6];
    char tempStr[5];
    char hoursStr[6];

    if (rpmValid) {
        snprintf(rpmStr, sizeof(rpmStr), "%05d", (int)data.rpm);
    } else {
        setPlaceholder(rpmStr, sizeof(rpmStr), sizeof(rpmStr) - 1);
    }

    if (torqueValid) {
        snprintf(torqueStr, sizeof(torqueStr), "%04d", (int)data.torque);
    } else {
        setPlaceholder(torqueStr, sizeof(torqueStr), sizeof(torqueStr) - 1);
    }

    if (fuelValid) {
        snprintf(fuelStr, sizeof(fuelStr), "%05.1f", data.fuelConsumption);
    } else {
        setPlaceholder(fuelStr, sizeof(fuelStr), sizeof(fuelStr) - 1);
    }

    if (tempValid) {
        snprintf(tempStr, sizeof(tempStr), "%04d", (int)data.engineTemperature);
    } else {
        setPlaceholder(tempStr, sizeof(tempStr), sizeof(tempStr) - 1);
    }

    if (hoursValid) {
        snprintf(hoursStr, sizeof(hoursStr), "%05d", (int)data.engineHours);
    } else {
        setPlaceholder(hoursStr, sizeof(hoursStr), sizeof(hoursStr) - 1);
    }

    char mensaje[64];
    snprintf(mensaje, sizeof(mensaje), ">STX08,%s%s%s%s%s<",
             rpmStr, torqueStr, fuelStr, tempStr, hoursStr);
    outMessage = std::string(mensaje);
    return true;
}

static void buildDateTimeHex(const IsobusData& data, uint32_t nowMs, unsigned long timeoutMs, char* out13) {
    if (data.hasDateTime && isFresh(data.dateTimeTs, nowMs, timeoutMs)) {
        snprintf(out13, 13, "%02X%02X%02X%02X%02X%02X",
                 data.seconds, data.minutes, data.hours, data.month, data.day, data.year);
    } else {
        snprintf(out13, 13, "~~~~~~~~~~~~");
    }
}

bool SendTaskCanIsobus::buildMessage1(const IsobusData& data, uint32_t nowMs, std::string& outMessage) {
    const unsigned long timeoutPosition = 10000;
    const unsigned long timeoutDateTime = 10000;
    const unsigned long timeoutSpeedDirection = 10000;
    const unsigned long timeoutProcessVariable = 10000;

    const bool positionValid = data.hasPosition && isFresh(data.positionTs, nowMs, timeoutPosition);
    const bool speedValid = data.hasSpeedDirection && isFresh(data.speedDirectionTs, nowMs, timeoutSpeedDirection);
    const bool sectionsValid = data.hasStatusSections && isFresh(data.statusSectionsTs, nowMs, timeoutProcessVariable);
    const bool pressureP0Valid = data.hasPumpPressureP0 && isFresh(data.pumpPressureP0Ts, nowMs, timeoutProcessVariable);
    const bool pressureP1Valid = data.hasPumpPressureP1 && isFresh(data.pumpPressureP1Ts, nowMs, timeoutProcessVariable);
    const bool workStateValid = data.hasWorkState && isFresh(data.workStateTs, nowMs, timeoutProcessVariable);

    const bool solid0Valid = data.hasSolidP0 && isFresh(data.solidP0Ts, nowMs, timeoutProcessVariable);
    const bool solid1Valid = data.hasSolidP1 && isFresh(data.solidP1Ts, nowMs, timeoutProcessVariable);
    const bool liquid0Valid = data.hasLiquidP0 && isFresh(data.liquidP0Ts, nowMs, timeoutProcessVariable);
    const bool liquid1Valid = data.hasLiquidP1 && isFresh(data.liquidP1Ts, nowMs, timeoutProcessVariable);

    const bool meansWorkingValid = solid0Valid || solid1Valid || liquid0Valid || liquid1Valid;
    const bool meansWorkingActive =
        (solid0Valid && data.solidP0 != 0) ||
        (solid1Valid && data.solidP1 != 0) ||
        (liquid0Valid && data.liquidP0 != 0) ||
        (liquid1Valid && data.liquidP1 != 0);

    if (!(positionValid || speedValid || sectionsValid || pressureP0Valid || pressureP1Valid || workStateValid || meansWorkingValid)) {
        return false;
    }

    char dtHex[13];
    buildDateTimeHex(data, nowMs, timeoutDateTime, dtHex);

    char latitudeStr[9];
    if (positionValid) {
        snprintf(latitudeStr, sizeof(latitudeStr), "%08X", static_cast<unsigned int>(data.latitude));
    } else {
        setPlaceholder(latitudeStr, sizeof(latitudeStr));
    }

    char longitudeStr[9];
    if (positionValid) {
        snprintf(longitudeStr, sizeof(longitudeStr), "%08X", static_cast<unsigned int>(data.longitude));
    } else {
        setPlaceholder(longitudeStr, sizeof(longitudeStr));
    }

    char speedStr[5];
    if (speedValid) {
        snprintf(speedStr, sizeof(speedStr), "%04X", static_cast<unsigned int>(data.vehicleSpeed));
    } else {
        setPlaceholder(speedStr, sizeof(speedStr));
    }

    char headingStr[5];
    if (speedValid) {
        snprintf(headingStr, sizeof(headingStr), "%04X", static_cast<unsigned int>(data.compass));
    } else {
        setPlaceholder(headingStr, sizeof(headingStr));
    }

    char altitudeStr[5];
    if (speedValid) {
        snprintf(altitudeStr, sizeof(altitudeStr), "%04X", static_cast<unsigned int>(data.altitude));
    } else {
        setPlaceholder(altitudeStr, sizeof(altitudeStr));
    }

    char sectionsStr[9];
    if (sectionsValid) {
        snprintf(sectionsStr, sizeof(sectionsStr), "%08X", static_cast<unsigned int>(data.statusSections));
    } else {
        setPlaceholder(sectionsStr, sizeof(sectionsStr));
    }

    char pressureP0Str[9];
    if (pressureP0Valid) {
        snprintf(pressureP0Str, sizeof(pressureP0Str), "%08X", static_cast<unsigned int>(data.pumpPressureP0));
    } else {
        setPlaceholder(pressureP0Str, sizeof(pressureP0Str));
    }

    char pressureP1Str[9];
    if (pressureP1Valid) {
        snprintf(pressureP1Str, sizeof(pressureP1Str), "%08X", static_cast<unsigned int>(data.pumpPressureP1));
    } else {
        setPlaceholder(pressureP1Str, sizeof(pressureP1Str));
    }

    char workStateStr[9];
    if (workStateValid) {
        snprintf(workStateStr, sizeof(workStateStr), "%08X", static_cast<unsigned int>(data.workState));
    } else {
        setPlaceholder(workStateStr, sizeof(workStateStr));
    }

    char meansWorkingStr[4];
    if (meansWorkingValid) {
        snprintf(meansWorkingStr, sizeof(meansWorkingStr), "%u", static_cast<unsigned int>(meansWorkingActive ? 1U : 0U));
    } else {
        setPlaceholder(meansWorkingStr, sizeof(meansWorkingStr));
    }

    char mensaje1[256];
    snprintf(mensaje1, sizeof(mensaje1),
             ">STX03,IS1:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s<",
             dtHex,
             latitudeStr, longitudeStr, speedStr, headingStr, altitudeStr,
             sectionsStr, pressureP0Str, pressureP1Str, workStateStr, meansWorkingStr);

    outMessage = std::string(mensaje1);
    return true;
}

bool SendTaskCanIsobus::buildMessage2(const IsobusData& data, uint32_t nowMs, std::string& outMessage) {
    const unsigned long timeoutPosition = 10000;
    const unsigned long timeoutDateTime = 10000;
    const unsigned long timeoutSpeedDirection = 10000;
    const unsigned long timeoutProcessVariable = 10000;

    const bool setSolid0Valid = data.hasSetSolidP0 && isFresh(data.setSolidP0Ts, nowMs, timeoutProcessVariable);
    const bool solid0Valid = data.hasSolidP0 && isFresh(data.solidP0Ts, nowMs, timeoutProcessVariable);
    const bool setSolid1Valid = data.hasSetSolidP1 && isFresh(data.setSolidP1Ts, nowMs, timeoutProcessVariable);
    const bool solid1Valid = data.hasSolidP1 && isFresh(data.solidP1Ts, nowMs, timeoutProcessVariable);
    const bool setLiquid0Valid = data.hasSetLiquidP0 && isFresh(data.setLiquidP0Ts, nowMs, timeoutProcessVariable);
    const bool liquid0Valid = data.hasLiquidP0 && isFresh(data.liquidP0Ts, nowMs, timeoutProcessVariable);
    const bool setLiquid1Valid = data.hasSetLiquidP1 && isFresh(data.setLiquidP1Ts, nowMs, timeoutProcessVariable);
    const bool liquid1Valid = data.hasLiquidP1 && isFresh(data.liquidP1Ts, nowMs, timeoutProcessVariable);
    const bool sectionsValid = data.hasStatusSections && isFresh(data.statusSectionsTs, nowMs, timeoutProcessVariable);

    if (!(setSolid0Valid || solid0Valid || setSolid1Valid || solid1Valid ||
          setLiquid0Valid || liquid0Valid || setLiquid1Valid || liquid1Valid || sectionsValid)) {
        return false;
    }

    char dtHex[13];
    buildDateTimeHex(data, nowMs, timeoutDateTime, dtHex);

    char setSolid0Str[9];
    if (setSolid0Valid) {
        snprintf(setSolid0Str, sizeof(setSolid0Str), "%08X", static_cast<unsigned int>(data.setSolidP0));
    } else {
        setPlaceholder(setSolid0Str, sizeof(setSolid0Str));
    }

    char solid0Str[9];
    if (solid0Valid) {
        snprintf(solid0Str, sizeof(solid0Str), "%08X", static_cast<unsigned int>(data.solidP0));
    } else {
        setPlaceholder(solid0Str, sizeof(solid0Str));
    }

    char setSolid1Str[9];
    if (setSolid1Valid) {
        snprintf(setSolid1Str, sizeof(setSolid1Str), "%08X", static_cast<unsigned int>(data.setSolidP1));
    } else {
        setPlaceholder(setSolid1Str, sizeof(setSolid1Str));
    }

    char solid1Str[9];
    if (solid1Valid) {
        snprintf(solid1Str, sizeof(solid1Str), "%08X", static_cast<unsigned int>(data.solidP1));
    } else {
        setPlaceholder(solid1Str, sizeof(solid1Str));
    }

    char setLiquid0Str[9];
    if (setLiquid0Valid) {
        snprintf(setLiquid0Str, sizeof(setLiquid0Str), "%08X", static_cast<unsigned int>(data.setLiquidP0));
    } else {
        setPlaceholder(setLiquid0Str, sizeof(setLiquid0Str));
    }

    char liquid0Str[9];
    if (liquid0Valid) {
        snprintf(liquid0Str, sizeof(liquid0Str), "%08X", static_cast<unsigned int>(data.liquidP0));
    } else {
        setPlaceholder(liquid0Str, sizeof(liquid0Str));
    }

    char setLiquid1Str[9];
    if (setLiquid1Valid) {
        snprintf(setLiquid1Str, sizeof(setLiquid1Str), "%08X", static_cast<unsigned int>(data.setLiquidP1));
    } else {
        setPlaceholder(setLiquid1Str, sizeof(setLiquid1Str));
    }

    char liquid1Str[9];
    if (liquid1Valid) {
        snprintf(liquid1Str, sizeof(liquid1Str), "%08X", static_cast<unsigned int>(data.liquidP1));
    } else {
        setPlaceholder(liquid1Str, sizeof(liquid1Str));
    }

    char sectionsStr[9];
    if (sectionsValid) {
        snprintf(sectionsStr, sizeof(sectionsStr), "%08X", static_cast<unsigned int>(data.statusSections));
    } else {
        setPlaceholder(sectionsStr, sizeof(sectionsStr));
    }

    char mensaje2[256];
    snprintf(mensaje2, sizeof(mensaje2),
             ">STX13,IS2:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s<",
             dtHex,
             setSolid0Str, solid0Str, setSolid1Str, solid1Str,
             setLiquid0Str, liquid0Str, setLiquid1Str, liquid1Str,
             sectionsStr);

    outMessage = std::string(mensaje2);
    return true;
}

bool SendTaskCanVg55r::buildMessage(const Vg55rData& data, std::string& outMessage) {
    if (!data.valid) {
        return false;
    }
    char vgMsg[128];
    snprintf(vgMsg, sizeof(vgMsg), ">STX13,VG5:%06.2f:%06.2f:%07.2f:%lu<",
             data.pitch, data.roll, data.yaw, (unsigned long)data.timestamp);
    outMessage = std::string(vgMsg);
    return true;
}
