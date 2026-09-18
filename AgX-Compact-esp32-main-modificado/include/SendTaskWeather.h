#pragma once

#include "WeatherTask.h"
#include <string>

class SendTaskWeather {
public:
    static bool isValid(const WeatherData& data);
    static std::string buildStx06(const WeatherData& data);
    static std::string buildStx15(const WeatherData& data);
};
