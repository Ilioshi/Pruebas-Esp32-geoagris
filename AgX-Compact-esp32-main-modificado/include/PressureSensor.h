#pragma once
#include <Arduino.h>
#include <string>

// Clase para manejar la comunicación con el sensor de presión a través de Modbus
class PressureSensor {
public:
    // Constructor
    PressureSensor();

    // Método para obtener los datos del sensor de presión
    // Retorna true si se obtuvo correctamente la lectura
    bool getData();

    // Obtener la última lectura de presión
    float getPressure() const;

    // Modo de prueba/desarrollo para establecer valores fijos
    void setForceMode(bool force, float forcedValue = 0.0);

private:
    // Parámetros Modbus del sensor
    static const uint8_t SLAVE_ADDRESS = 0x7B;  // Dirección 123 (0x7B)
    static const uint16_t REGISTER_ADDRESS = 0x0000;  // Registro inicial
    static const uint16_t NUM_REGISTERS = 2;  // Leer 2 registros

    // Valor almacenado de presión
    float m_pressure;

    // Datos crudos recibidos de Modbus
    uint8_t m_raw_data[4];  // 2 registros = 4 bytes

    // Modo de prueba/desarrollo
    bool m_force_mode;
    float m_forced_value;

    // Método para convertir los datos crudos a valor de presión
    void processRawData();
};