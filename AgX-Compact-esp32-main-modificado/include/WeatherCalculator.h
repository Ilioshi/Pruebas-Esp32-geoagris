#pragma once
#include <Arduino.h>
#include <string>

// Clase para realizar cálculos meteorológicos y condiciones climáticas
class WeatherCalculator {
public:
    // Constructor
    WeatherCalculator();

    // Calcular DeltaT usando temperatura, humedad y presión
    double calculateDeltaT(double temperature, double relativeHumidity, double pressure);
    
    // Calcular el tamaño óptimo de gota basado en el DeltaT
    double calculateOptimalDropSize(double deltaT);
    
    // Evaluar la condición climática general (1=buena, 2=precaución, 3=no pulverizar)
    int evaluateGeneralCondition(double windSpeed, double temperature, double relativeHumidity, double pressure);
    
    // Evaluar la condición de velocidad del viento (1=buena, 2=precaución, 3=no pulverizar)
    int evaluateWindSpeedCondition(double windSpeed);
    
    // Evaluar la condición de DeltaT (1=buena, 2=precaución, 3=no pulverizar)
    int evaluateDeltaTCondition(double deltaT);
    
    // Evaluar la condición de temperatura (1=buena, 2=precaución, 3=no pulverizar)
    int evaluateTemperatureCondition(double temperature);
    
    // Calcular la velocidad verdadera del viento
    double calculateTrueWindSpeed(double apparentWindSpeed, double apparentWindDirection, double travelSpeed);
    
    // Calcular la dirección verdadera del viento
    double calculateTrueWindDirection(double apparentWindSpeed, double apparentWindDirection, 
                                    double travelSpeed, double travelDirection, double compass);
    
    // Obtener texto descriptivo para la dirección del viento (N, NE, E, etc.)
    std::string getWindDirectionText(double windDirection);
    
    // Obtener texto descriptivo para la condición climática
    std::string explainCondition(int condition);
    
    // Obtener texto descriptivo para la condición de DeltaT
    std::string explainDeltaT(double deltaT);
    
    // Obtener texto descriptivo para la condición de temperatura
    std::string explainAirTemp(double temperature);
    
    // Obtener texto descriptivo para la condición de velocidad del viento
    std::string explainWindSpeed(double windSpeed);
    
    // Construir mensaje STX15 con todos los datos y cálculos
    std::string buildSTX15Message(const std::string& gpsTimeString, 
                                 double windSpeed, double pressure,
                                 double temperature, double relativeHumidity,
                                 double windDirection, double travelSpeed,
                                 double travelDirection, double compass);
    
private:
    // Métodos auxiliares para el cálculo de temperatura de bulbo húmedo
    double calculateWetBulbTempHsuanYuChen(double temperature, double relativeHumidity);
    double calculateWetBulbTempStull(double temperature, double relativeHumidity);
};