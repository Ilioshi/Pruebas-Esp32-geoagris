#include "PressureSensor.h"
#include "rs485_utils.h"

// Constructor
PressureSensor::PressureSensor() : 
    m_pressure(0.0),
    m_force_mode(false),
    m_forced_value(0.0) {
    
    // Inicializar el array de datos crudos
    memset(m_raw_data, 0, sizeof(m_raw_data));
}

// Método para obtener datos del sensor
bool PressureSensor::getData() {
    // Si estamos en modo de prueba, usar el valor forzado
    if (m_force_mode) {
        m_pressure = m_forced_value;
        return true;
    }
    
    // Realizar la consulta Modbus al sensor de presión
    bool read_ok = rs485ModbusAsk(SLAVE_ADDRESS, REGISTER_ADDRESS, NUM_REGISTERS, m_raw_data);
    
    if (read_ok) {
        // Procesar los datos recibidos para obtener el valor de presión
        processRawData();
    }
    
    return read_ok;
}

// Procesar los datos crudos para obtener la presión
void PressureSensor::processRawData() {
    // Nota: Asumiendo que los datos vienen como float en los 4 bytes (2 registros)
    // Dependiendo del formato específico del sensor, este método podría necesitar ajustes
    
    // Método 1: Si los datos vienen como flotante IEEE 754 (común en muchos sensores)
    union {
        uint8_t bytes[4];
        float value;
    } converter;
    
    // Copiar los bytes en el orden correcto (podría requerir ajustes según el sensor)
    // Orden Big Endian (más común en Modbus)
    converter.bytes[0] = m_raw_data[3];
    converter.bytes[1] = m_raw_data[2];
    converter.bytes[2] = m_raw_data[1];
    converter.bytes[3] = m_raw_data[0];
    
    m_pressure = converter.value;
    
    // Alternativa: si los datos vienen como entero escalado
    // Ejemplo: uint32_t rawValue = (m_raw_data[0] << 24) | (m_raw_data[1] << 16) | (m_raw_data[2] << 8) | m_raw_data[3];
    // m_pressure = rawValue * 0.001f; // Factor de escala según documentación del sensor
}

// Obtener el valor de presión actual
float PressureSensor::getPressure() const {
    return m_pressure;
}

// Configurar modo de prueba con valor forzado
void PressureSensor::setForceMode(bool force, float forcedValue) {
    m_force_mode = force;
    if (force) {
        m_forced_value = forcedValue;
    }
}