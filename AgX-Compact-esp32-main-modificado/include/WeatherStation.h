#pragma once
#include <Arduino.h>
#include <string>

// Clase para manejar la comunicación con la estación meteorológica
class WeatherStation {
public:
    // Constructor
    WeatherStation();

    // Método para obtener datos de la estación
    // Retorna true si obtuvo datos válidos
    bool getData();

    // Forzar valores para pruebas (solo para desarrollo/debug)
    void setForceMode(bool force);

    // Getters para los datos meteorológicos
    double getAirTemp() const;
    double getRelativeMoisture() const;
    double getPressure() const;
    double getWindSpeed() const;
    double getWindDirection() const;
    double getCompass() const;
    double getPrecipitation() const;

    // Método para construir mensaje STX06 con datos crudos
    std::string buildSTX06Message() const;

private:
    // Variables para almacenar datos meteorológicos
    double m_air_temp_avg;
    double m_relative_moisture_avg;
    double m_pressure;
    double m_wind_speed_max;
    double m_wind_direction;
    double m_compass;
    double m_precipitation;

    // Datos crudos recibidos de la estación
    uint8_t m_weather_data_0[16];
    uint8_t m_weather_data_1[10];
    uint8_t m_weather_data_2[30];

    // Modo de prueba/desarrollo
    bool m_force_weather;
};