#include "SendTaskWeather.h"
#include "WeatherCalculator.h"
#include "agx_utils.h"

bool SendTaskWeather::isValid(const WeatherData& data) {
    return data.valid;
}

std::string SendTaskWeather::buildStx06(const WeatherData& data) {
    std::string message = ">STX06,HAN:";
    double windSpeedKmh = data.windSpeed;
    double windSpeedMs = windSpeedKmh < 3.0 ? 0.0 : windSpeedKmh / 3.6;

    message += toBase64UrlSafe(data.airTemp, -50, 100, 2);
    message += toBase64UrlSafe(data.relativeHumidity, 0, 100, 2);
    message += toBase64UrlSafe(data.pressure, 0, 1200, 2);
    message += toBase64UrlSafe(windSpeedMs, 0, 200, 2);
    message += toBase64UrlSafe(data.windDirection, 0, 360, 2);
    message += toBase64UrlSafe(data.compass, 0, 360, 2);
    message += toBase64UrlSafe(data.precipitation, 0, 1000, 2);

    message += "<";
    return message;
}

std::string SendTaskWeather::buildStx15(const WeatherData& data) {
    static WeatherCalculator calculator;
    return calculator.buildSTX15Message(
        data.gpsTime,
        data.windSpeed,
        data.pressure,
        data.airTemp,
        data.relativeHumidity,
        data.windDirection,
        data.gpsSpeedKmh,
        data.gpsHeading,
        data.compass);
}
