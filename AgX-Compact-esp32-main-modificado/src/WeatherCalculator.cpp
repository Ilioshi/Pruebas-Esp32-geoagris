#include "WeatherCalculator.h"

// Constructor
WeatherCalculator::WeatherCalculator() {
    // Nada que inicializar por ahora
}

// Calcular DeltaT
double WeatherCalculator::calculateDeltaT(double temperature, double relativeHumidity, double pressure) {
    double wetBulbTemp;
    
    // Decisión de qué fórmula utilizar según las condiciones
    if (temperature >= 20 && temperature <= 45 && relativeHumidity >= 40) {
        // En estas condiciones es mejor usar esta función
        wetBulbTemp = calculateWetBulbTempHsuanYuChen(temperature, relativeHumidity);
    } else {
        wetBulbTemp = calculateWetBulbTempStull(temperature, relativeHumidity);
    }
    
    // Calcular y devolver el Delta T
    return temperature - wetBulbTemp;
}

// Calcular temperatura de bulbo húmedo (Hsuan-yu Chen)
double WeatherCalculator::calculateWetBulbTempHsuanYuChen(double temperature, double relativeHumidity) {
    return -4.391976 + 0.0198197 * relativeHumidity + 0.526359 * temperature + 
           0.00730271 * relativeHumidity * temperature + 
           2.4315e-4 * pow(relativeHumidity, 2) - 
           2.58101e-5 * temperature * pow(relativeHumidity, 2);
}

// Calcular temperatura de bulbo húmedo (Stull)
double WeatherCalculator::calculateWetBulbTempStull(double temperature, double relativeHumidity) {
    return temperature * atan(0.151977 * pow(relativeHumidity + 8.313659, 0.5)) +
           atan(temperature + relativeHumidity) -
           atan(relativeHumidity - 1.676331) +
           0.00391838 * pow(relativeHumidity, 1.5) * atan(0.023101 * relativeHumidity) -
           4.686035;
}

// Calcular el tamaño óptimo de gota
double WeatherCalculator::calculateOptimalDropSize(double deltaT) {
    if (deltaT <= 0) {
        return 0;
    } else {
        return round(sqrt(90.0 * deltaT * 80.0));
    }
}

// Evaluar la condición de velocidad del viento
int WeatherCalculator::evaluateWindSpeedCondition(double windSpeed) {
    if (windSpeed > 20) {
        return 3;
    } else if (windSpeed <= 20 && windSpeed > 15) {
        return 2;
    } else if (windSpeed <= 15 && windSpeed > 5) {
        return 1;
    } else if (windSpeed <= 5 && windSpeed > 3) {
        return 2;
    } else if (windSpeed <= 3) {
        return 2;
    }
    
    return 2; // Valor por defecto
}

// Evaluar la condición de DeltaT
int WeatherCalculator::evaluateDeltaTCondition(double deltaT) {
    if (deltaT < 2) {
        return 2;
    } else if (deltaT < 8 && deltaT >= 2) {
        return 1;
    } else if (deltaT < 10 && deltaT >= 8) {
        return 2;
    } else if (deltaT >= 10) {
        return 3;
    }
    
    return 2; // Valor por defecto
}

// Evaluar la condición de temperatura
int WeatherCalculator::evaluateTemperatureCondition(double temperature) {
    if (temperature >= 3) {
        return 1; // Condición verde
    } else if (temperature < 3 && temperature > 0) {
        return 2; // Condición amarilla
    } else if (temperature <= 0) {
        return 3; // Condición roja
    }
    
    return 1; // Valor por defecto
}

// Evaluar la condición climática general
int WeatherCalculator::evaluateGeneralCondition(double windSpeed, double temperature, double relativeHumidity, double pressure) {
    int windSpeedCondition = evaluateWindSpeedCondition(windSpeed);
    int deltaTCondition = evaluateDeltaTCondition(calculateDeltaT(temperature, relativeHumidity, pressure));
    int temperatureCondition = evaluateTemperatureCondition(temperature);
    
    // Devuelve el mayor valor entre las tres condiciones
    return max(max(windSpeedCondition, deltaTCondition), temperatureCondition);
}

// Calcular la velocidad verdadera del viento
double WeatherCalculator::calculateTrueWindSpeed(double apparentWindSpeed, double apparentWindDirection, double travelSpeed) {
    return sqrt(pow(apparentWindSpeed, 2) + pow(travelSpeed, 2) -
                2 * apparentWindSpeed * travelSpeed * cos(radians(apparentWindDirection)));
}

// Calcular la dirección verdadera del viento
double WeatherCalculator::calculateTrueWindDirection(double apparentWindSpeed, double apparentWindDirection, 
                                               double travelSpeed, double travelDirection, double compass) {
    auto normalize360 = [](double angle) -> double {
        angle = fmod(angle, 360.0);
        if (angle < 0) {
            angle += 360.0;
        }
        if (angle >= 360.0 - 1e-9) {
            angle = 0.0;
        }
        return angle;
    };
    
    // Si la velocidad de desplazamiento es menor a 3 km/h, ajusta la dirección del viento
    if (travelSpeed < 3) {
        return normalize360(apparentWindDirection + compass);
    }

    // Replica la lógica de clima.wind_direction_true(...) en la base.
    (void)compass;

    double apparentFromWorld = normalize360(travelDirection + apparentWindDirection);
    double apparentToWorld = normalize360(apparentFromWorld + 180.0);

    double apparentX = apparentWindSpeed * sin(radians(apparentToWorld));
    double apparentY = apparentWindSpeed * cos(radians(apparentToWorld));

    double travelX = travelSpeed * sin(radians(travelDirection));
    double travelY = travelSpeed * cos(radians(travelDirection));

    double trueX = apparentX + travelX;
    double trueY = apparentY + travelY;
    double trueSpeed = sqrt(trueX * trueX + trueY * trueY);

    if (trueSpeed < 1e-9) {
        return 0.0;
    }

    double result = degrees(atan2(-trueX, -trueY));
    return normalize360(result);
}

// Obtener texto descriptivo para la dirección del viento
std::string WeatherCalculator::getWindDirectionText(double windDirection) {
    if ((windDirection >= 0 && windDirection < 22.5) ||
        (windDirection <= 360 && windDirection > 337.5)) {
        return "N";
    } else if (windDirection >= 22.5 && windDirection < 67.5) {
        return "NE";
    } else if (windDirection >= 67.5 && windDirection < 112.5) {
        return "E";
    } else if (windDirection >= 112.5 && windDirection < 157.5) {
        return "SE";
    } else if (windDirection >= 157.5 && windDirection < 202.5) {
        return "S";
    } else if (windDirection >= 202.5 && windDirection < 247.5) {
        return "SW";
    } else if (windDirection >= 247.5 && windDirection < 292.5) {
        return "W";
    } else if (windDirection >= 292.5 && windDirection < 337.5) {
        return "NW";
    } else {
        return "Invalid"; // En caso de que la dirección no esté en el rango esperado
    }
}

// Obtener texto descriptivo para la condición climática
std::string WeatherCalculator::explainCondition(int condition) {
    if (condition == 1) {
        return "Good Weather";
    } else if (condition == 2) {
        return "Take care";
    } else {
        return "Do not spray";
    }
}

// Obtener texto descriptivo para la condición de DeltaT
std::string WeatherCalculator::explainDeltaT(double deltaT) {
    if (deltaT < 2) {
        return "Low";
    } else if (deltaT < 8) {
        return "ok";
    } else if (deltaT < 10) {
        return "High";
    } else {
        return "Very High";
    }
}

// Obtener texto descriptivo para la condición de temperatura
std::string WeatherCalculator::explainAirTemp(double temperature) {
    if (temperature <= 0) {
        return "Very Low";
    } else if (temperature < 3) {
        return "Low";
    } else {
        return "ok";
    }
}

// Obtener texto descriptivo para la condición de velocidad del viento
std::string WeatherCalculator::explainWindSpeed(double windSpeed) {
    if (windSpeed <= 3) {
        return "Very Low";
    } else if (windSpeed <= 5) {
        return "Low";
    } else if (windSpeed <= 15) {
        return "ok";
    } else if (windSpeed <= 20) {
        return "High";
    } else {
        return "Very High";
    }
}

// Construir mensaje STX15 con todos los datos y cálculos
std::string WeatherCalculator::buildSTX15Message(const std::string& gpsTimeString, 
                                            double windSpeed, double pressure,
                                            double temperature, double relativeHumidity,
                                            double windDirection, double travelSpeed,
                                            double travelDirection, double compass) {
    // Calcular las condiciones climáticas
    double deltaT = calculateDeltaT(temperature, relativeHumidity, pressure);
    int deltaTCondition = evaluateDeltaTCondition(deltaT);
    int temperatureCondition = evaluateTemperatureCondition(temperature);
    
    double trueWindSpeed = calculateTrueWindSpeed(windSpeed, windDirection, travelSpeed);
    int windSpeedCondition = evaluateWindSpeedCondition(trueWindSpeed);
    
    double trueWindDirection = calculateTrueWindDirection(windSpeed, windDirection, travelSpeed, travelDirection, compass);
    std::string windDirectionText = getWindDirectionText(trueWindDirection);
    
    int climateCondition = evaluateGeneralCondition(trueWindSpeed, temperature, relativeHumidity, pressure);
    double dropSize = calculateOptimalDropSize(deltaT);
    
    // Obtener explicaciones de condiciones
    std::string explainedCondition = explainCondition(climateCondition);
    std::string explainedDeltaT = explainDeltaT(deltaT);
    std::string explainedAirTemp = explainAirTemp(temperature);
    std::string explainedWindSpeed = explainWindSpeed(trueWindSpeed);
    
    // Formatear números a string con precisión específica
    char buffer[32];
    
    sprintf(buffer, "%d", climateCondition);
    std::string climateConditionStr = buffer;
    
    sprintf(buffer, "%d", deltaTCondition);
    std::string deltaTConditionStr = buffer;
    
    sprintf(buffer, "%.1f", deltaT);
    std::string deltaTStr = buffer;
    
    sprintf(buffer, "%d", int(round(dropSize)));
    std::string dropSizeStr = buffer;
    
    sprintf(buffer, "%d", temperatureCondition);
    std::string temperatureConditionStr = buffer;
    
    sprintf(buffer, "%.1f", temperature);
    std::string temperatureStr = buffer;
    
    sprintf(buffer, "%d", int(round(relativeHumidity)));
    std::string relativeHumidityStr = buffer;
    
    sprintf(buffer, "%d", windSpeedCondition);
    std::string windSpeedConditionStr = buffer;
    
    sprintf(buffer, "%d", int(round(trueWindSpeed)));
    std::string trueWindSpeedStr = buffer;
    
    sprintf(buffer, "%d", int(round(trueWindDirection)));
    std::string trueWindDirectionStr = buffer;
    
    // Construir el mensaje final
    std::string message = ">STX15," + gpsTimeString +
                     ";" + climateConditionStr +
                     ";" + explainedCondition +
                     ";" + deltaTConditionStr +
                     ";" + deltaTStr +
                     ";" + explainedDeltaT +
                     ";" + dropSizeStr +
                     ";" + temperatureConditionStr +
                     ";" + temperatureStr +
                     ";" + explainedAirTemp +
                     ";" + relativeHumidityStr +
                     ";" + windSpeedConditionStr +
                     ";" + trueWindSpeedStr +
                     ";" + explainedWindSpeed +
                     ";" + windDirectionText +
                     ";" + trueWindDirectionStr +
                     ";0;0;<";
    
    return message;
}
