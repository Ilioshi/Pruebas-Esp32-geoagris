#include "WeatherStation.h"
#include "rs485_utils.h"
#include "agx_utils.h"

// Constructor
WeatherStation::WeatherStation() : 
    m_air_temp_avg(0),
    m_relative_moisture_avg(0),
    m_pressure(0),
    m_wind_speed_max(0),
    m_wind_direction(0),
    m_compass(0),
    m_precipitation(0),
    m_force_weather(false) {
    
    // Inicializar arreglos de datos
    memset(m_weather_data_0, 0, sizeof(m_weather_data_0));
    memset(m_weather_data_1, 0, sizeof(m_weather_data_1));
    memset(m_weather_data_2, 0, sizeof(m_weather_data_2));
}

// Método para obtener datos de la estación
bool WeatherStation::getData() {
    // Parámetros para las consultas Modbus
    uint8_t slaveAddress = 0xFF;     // Dirección del dispositivo estación meteorológica
    
    // Dirección de los registros y cantidad a leer 
    uint16_t firstRegister1 = 0x0009; // Primer bloque - registro inicial 0x0009
    uint16_t numRegisters1 = 6;       // Leer 6 registros (incluye precipitación en 0x000E)
    
    uint16_t firstRegister2 = 0x0020; // Segundo bloque - registro inicial 0x0020 (para el compass)
    uint16_t numRegisters2 = 1;       // Leer 1 registro (compass)
    
    // Realizar las consultas Modbus para obtener los datos de la estación
    bool read_ok = (rs485ModbusAsk(slaveAddress, firstRegister1, numRegisters1, m_weather_data_0) && 
                     rs485ModbusAsk(slaveAddress, firstRegister2, numRegisters2, m_weather_data_1));

    if (m_force_weather) {
        read_ok = true;
        m_weather_data_0[0] = 0x18;
        m_weather_data_0[1] = 0xB7;
        m_weather_data_0[2] = 0x0B;
        m_weather_data_0[3] = 0xA3;
        m_weather_data_0[4] = 0x27;
        m_weather_data_0[5] = 0x9D;
        m_weather_data_0[6] = 0x00;
        m_weather_data_0[7] = 0x7E;
        m_weather_data_0[8] = 0x0D;
        m_weather_data_0[9] = 0x83;
        m_weather_data_0[10] = 0x00; // Precipitación (0x000E) - 0x0032 = 5.0 mm
        m_weather_data_0[11] = 0x32;
        m_weather_data_1[0] = 0x00; // Compass (0x0020) - 0x0045
        m_weather_data_1[1] = 0x45;
    }
    
    if (read_ok) {
        // Actualizar variables
        uint16_t temp_value = (m_weather_data_0[0] << 8) | m_weather_data_0[1];
        m_air_temp_avg = fromUnsignedInt(temp_value, -40.0, 615.35, 16); // Rango: -40 a 615.35 (0-65535 / 100)
        
        uint16_t moisture_value = (m_weather_data_0[2] << 8) | m_weather_data_0[3];
        m_relative_moisture_avg = fromUnsignedInt(moisture_value, 0.0, 655.35, 16); // Rango: 0 a 655.35 (0-65535 / 100)
        
        uint16_t pressure_value = (m_weather_data_0[4] << 8) | m_weather_data_0[5];
        m_pressure = fromUnsignedInt(pressure_value, 0.0, 6553.5, 16); // Rango: 0 a 6553.5 (0-65535 / 10)
        
        uint16_t wind_speed_value = (m_weather_data_0[6] << 8) | m_weather_data_0[7];
        m_wind_speed_max = fromUnsignedInt(wind_speed_value, 0.0, 655.35, 16) * 3.6; // Rango: 0 a 655.35 (0-65535 / 100) * 3.6

        uint16_t wind_dir_value = (m_weather_data_0[8] << 8) | m_weather_data_0[9];
        m_wind_direction = fromUnsignedInt(wind_dir_value, 0.0, 6553.5, 16); // Rango: 0 a 6553.5 (0-65535 / 10)
        
        uint16_t compass_value = (m_weather_data_1[0] << 8) | m_weather_data_1[1];
        m_compass = fromUnsignedInt(compass_value, 0.0, 65535.0, 16); // Rango: 0 a 65535

        // Leer precipitación desde registro 0x000E (ampliado 10 veces)
        uint16_t precipitation_value = (m_weather_data_0[10] << 8) | m_weather_data_0[11];
        m_precipitation = fromUnsignedInt(precipitation_value, 0.0, 6553.5, 16); // Rango: 0 a 6553.5 (0-65535 / 10)
    }
    
    return read_ok;
}

// Getter para la temperatura del aire
double WeatherStation::getAirTemp() const {
    return m_air_temp_avg;
}

// Getter para la humedad relativa
double WeatherStation::getRelativeMoisture() const {
    return m_relative_moisture_avg;
}

// Getter para la presión atmosférica
double WeatherStation::getPressure() const {
    return m_pressure;
}

// Getter para la velocidad del viento
double WeatherStation::getWindSpeed() const {
    return m_wind_speed_max;
}

// Getter para la dirección del viento
double WeatherStation::getWindDirection() const {
    return m_wind_direction;
}

// Getter para el compass
double WeatherStation::getCompass() const {
    return m_compass;
}

// Getter para la precipitación
double WeatherStation::getPrecipitation() const {
    return m_precipitation;
}

// Configurar forzar valores para pruebas
void WeatherStation::setForceMode(bool force) {
    m_force_weather = force;
}

// Construir mensaje STX06 con codificación base64 URL-safe
std::string WeatherStation::buildSTX06Message() const {
    std::string message = ">STX06,HAN:";
    double windSpeedKmh = m_wind_speed_max;
    double windSpeedMs = windSpeedKmh < 3.0 ? 0.0 : windSpeedKmh / 3.6;
    
    // Agregar temperatura codificada en base64 URL-safe
    message += toBase64UrlSafe(m_air_temp_avg, -50, 100, 2);
    
    // Agregar humedad relativa codificada en base64 URL-safe
    message += toBase64UrlSafe(m_relative_moisture_avg, 0, 100, 2);
    
    // Agregar presión codificada en base64 URL-safe
    message += toBase64UrlSafe(m_pressure, 0, 1200, 2);
    
    // Agregar velocidad del viento codificada en base64 URL-safe (convertida a m/s)
    message += toBase64UrlSafe(windSpeedMs, 0, 200, 2);
    
    // Agregar dirección del viento codificada en base64 URL-safe
    message += toBase64UrlSafe(m_wind_direction, 0, 360, 2);
    
    // Agregar compass codificado en base64 URL-safe
    message += toBase64UrlSafe(m_compass, 0, 360, 2);
    
    // Agregar precipitación codificada en base64 URL-safe
    message += toBase64UrlSafe(m_precipitation, 0, 1000, 2);
    
    message += "<";
    return message;
}