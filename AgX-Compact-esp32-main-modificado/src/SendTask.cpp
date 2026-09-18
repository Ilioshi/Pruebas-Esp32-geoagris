#include "SendTask.h"
#include "SendTaskCan.h"
#include "SendTaskWeather.h"
#include "SendTaskPulse.h"
#include "SendTaskPressure.h"
#include "SendTaskBeacon.h"
#include "WeatherTask.h"
#include "PressureTask.h"
#include "BeaconTask.h"

#ifdef ENABLE_PULSE
#include "Pulse.h"
#endif

#include "CAN/CAN_protocols/j1939_protocol.h"
#include "CAN/CAN_protocols/isobus_protocol.h"
#include "CAN/CAN.h"
#include "trax_utils.h"
#include "Messanger_queue.h"
#include "DutCanDiagnostic.h"

/**************************************************************
*                 Helper Functions Prototypes                 *
***************************************************************/
#ifdef ENABLE_SIMULATION
    static bool checkSimuWeather(const IsobusData& canData);
    static bool checkTimeStringUpdate(const std::string& timeStr);
    static std::string buildRtx15(std::string stx15);
#endif
/**************************************************************
*                         Main Task                           *
***************************************************************/

void sendTask(void* parameter) {
    const TickType_t period = pdMS_TO_TICKS(500);
    TickType_t lastWake = xTaskGetTickCount();

    uint32_t lastIsobusSent = 0;
    uint32_t lastWeatherSim = 0;
    uint32_t lastSimStx15Sent = 0;
    uint32_t lastWeatherSeq = 0;
    uint32_t lastPulseSeq = 0;
    uint32_t lastBeaconSeq = 0;
    uint32_t lastPressureSeq = 0;
    uint32_t lastVg55rSeq = 0;

    uint32_t lastJ1939Sent = 0;
    uint32_t lastVg55rSent = 0;

    while (true) {
        vTaskDelayUntil(&lastWake, period);

        const uint32_t nowMs = millis();

        IsobusData isobus;
        uint32_t isobusSeq = 0;
        if (isobusGetData(isobus, isobusSeq) && (nowMs - lastIsobusSent >= 1000)) {
            std::string msg1;
            std::string msg2;
            bool hasMsg1 = SendTaskCanIsobus::buildMessage1(isobus, nowMs, msg1);
            bool hasMsg2 = SendTaskCanIsobus::buildMessage2(isobus, nowMs, msg2);
            bool ok1 = true;
            bool ok2 = true;
            if (hasMsg1) {
                std::string resp = traxSendReceive(msg1);
                ok1 = !resp.empty();
            }
            if (hasMsg2) {
                std::string resp = traxSendReceive(msg2);
                ok2 = !resp.empty();
            }
            if (ok1 && ok2) {
                lastIsobusSent = nowMs;
            }
            if (!hasMsg1 && !hasMsg2) {
                lastIsobusSent = nowMs;
            }
        }
        
        WeatherData weather;
        uint32_t weatherSeq = 0;
        
        #ifdef ENABLE_SIMULATION 
        
        if (checkSimuWeather(isobus)) { // BUG: algunas veces no entra a esta parte
            // completo con los datos simulados
            weather.windSpeed = isobus.simWindSpeed;
            weather.windDirection = isobus.simWindDirection;
            weather.relativeHumidity = isobus.simHumidity;
            weather.airTemp = isobus.simTemperature;
            weather.compass = 0;            // no hay compass simulado
            weather.gpsHeading = 0;         // no hay gpsHeading simulado
            weather.gpsSpeedKmh = 0;        // no hay gpsSpeed simulado
            weather.gpsTime =  traxGpsTimeString();
            std::string stx15 = SendTaskWeather::buildStx15(weather);
            if (MessangerQueue_sendToSender(buildRtx15(stx15).c_str(),
                0, pdMS_TO_TICKS(1000))) {
                Serial.println("Simulated weather data sent to sender queue");
                isobusClearSimFlags();
                lastWeatherSim = nowMs;
            }

            if (nowMs - lastSimStx15Sent >= 1000) {
                traxSendReceive(stx15);
                lastSimStx15Sent = nowMs;
            }
        } else if (nowMs - lastWeatherSim >= 2000) {
        #endif

            if (weatherGetData(weather, weatherSeq) && 
                weatherSeq != lastWeatherSeq && SendTaskWeather::isValid(weather)) {
                std::string stx06 = SendTaskWeather::buildStx06(weather);
                std::string stx15 = SendTaskWeather::buildStx15(weather);
                std::string resp06 = traxSendReceive(stx06);
                std::string resp15 = traxSendReceive(stx15);
                if (!resp06.empty() && !resp15.empty()) {
                    lastWeatherSeq = weatherSeq;
                }
            }

        #ifdef ENABLE_SIMULATION
        }
        #endif

        PulseData pulse;
        uint32_t pulseSeq = 0;
        if (pulseGetData(pulse, pulseSeq) && pulseSeq != lastPulseSeq) {
            if (pulse.rate1 > 0.0f || pulse.rate2 > 0.0f) {
                std::string msg = SendTaskPulse::buildStx13(pulse);
                std::string resp = traxSendReceive(msg);
                if (!resp.empty()) {
                    lastPulseSeq = pulseSeq;
                }
            } else {
                lastPulseSeq = pulseSeq;
            }
        }

        #ifdef ENABLE_BEACON

        BeaconReportData beacons;
        uint32_t beaconSeq = 0;
        if (beaconGetData(beacons, beaconSeq) && beaconSeq != lastBeaconSeq) {
            std::string msg = SendTaskBeacon::buildStx13(beacons);
            if (!msg.empty()) {
                std::string resp = traxSendReceive(msg);
                if (!resp.empty()) {
                    lastBeaconSeq = beaconSeq;
                }
            }
        }

        #endif // ENABLE_BEACON

        PressureData pressure;
        uint32_t pressureSeq = 0;
        if (pressureGetData(pressure, pressureSeq) && pressureSeq != lastPressureSeq) {
            std::string msg = SendTaskPressure::buildStx09(pressure);
            std::string resp = traxSendReceive(msg);
            if (!resp.empty()) {
                lastPressureSeq = pressureSeq;
            }
        }

        J1939Data j1939;
        uint32_t j1939Seq = 0;
        const bool hasJ1939Data = j1939GetData(j1939, j1939Seq);
        // Weather/TRAX transactions above can block while CAN updates timestamps.
        // Use a fresh clock AFTER copying the snapshot, not the start of this loop.
        const uint32_t j1939Now = millis();
        if (hasJ1939Data) {
            if (j1939Now - lastJ1939Sent >= 1000) {
                std::string msg;
                if (SendTaskCanJ1939::buildMessage(j1939, j1939Now, msg)) {
                    std::string resp = traxSendReceive(msg);
                    dutStx08Transaction(j1939Seq, msg, resp);
                } else {
                    dutStx08State("STALE_OR_INVALID", j1939Seq);
                }
                lastJ1939Sent = j1939Now; // Bound retries even on error responses.
            }
        } else {
            // Includes no parsed data and a temporarily unavailable mutex snapshot.
            dutStx08State("NO_SNAPSHOT", j1939Seq);
        }

        Vg55rData vg55r;
        uint32_t vg55rSeq = 0;
        if (canGetVg55rData(vg55r, vg55rSeq) && (nowMs - lastVg55rSent >= 500)) {
            if (vg55rSeq != lastVg55rSeq && vg55r.valid) {
                std::string msg;
                if (SendTaskCanVg55r::buildMessage(vg55r, msg)) {
                    std::string resp = traxSendReceive(msg);
                    if (!resp.empty()) {
                        lastVg55rSent = nowMs;
                        lastVg55rSeq = vg55rSeq;
                    }
                }
            } else {
                lastVg55rSent = nowMs;
            }
        }
        dutCanDiagnosticTick();
    }
}


/**************************************************************
*                 Helper Functions Definitions                *
***************************************************************/

#ifdef ENABLE_SIMULATION
/**
 * @brief Check if ISOBUS data has any simulated weather fields
 * 
 * @param canData ISOBUS data structure to check
 * @return if any simulated weather fields are present
 */
static bool checkSimuWeather(const IsobusData& canData) {
    if (!canData.hasSimWindSpeed || !canData.hasSimWindDirection ||
        !canData.hasSimHumidity || !canData.hasSimTemperature) {
        return false;
    }

    return true;
}

/**
 * @brief check if time string is updated
 * 
 * @param timeStr current time string
 * @return true if updated, false otherwise
 */
static bool checkTimeStringUpdate(const std::string& timeStr) {
    // static variable to hold last time string
    static std::string lastTimeStr = "";
    if (timeStr != lastTimeStr) {
        lastTimeStr = timeStr;
        return true;
    }
    return false;
}

/**
 * @brief builds RTX15 from STX15
 * 
 * @param stx15 message in stx15 format
 * @return `std::string` RTX15 message
 */
static std::string buildRtx15(std::string stx15) {
    // RTX15 format: >RTX15,<STX15 without > and <><
    if (stx15.size() < 2 || stx15.front() != '>' || stx15.back() != '<') {
        return "";
    }
    std::string core = stx15.substr(1, stx15.size() - 2);
    return ">RTX15," + core + "<";
}

#endif
