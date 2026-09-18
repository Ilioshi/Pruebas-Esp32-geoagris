#pragma once
#include <Arduino.h>
#include <string>

// Función para enviar una consulta Modbus de lectura de registros y recibir la respuesta
// Retorna true si la operación fue exitosa
// slaveAddress: dirección del dispositivo esclavo (0-247)
// firstRegister: dirección del primer registro a leer
// numRegisters: cantidad de registros a leer
// responseReceived: buffer donde se almacenarán los datos recibidos (sin cabecera ni CRC)
bool rs485ModbusAsk(uint8_t slaveAddress, uint16_t firstRegister, uint16_t numRegisters, 
                    uint8_t *responseReceived);

// Función para calcular CRC-16 Modbus
uint16_t ModRTU_CRC(uint8_t *buf, int len);

// Inicializa los pines y configuración para RS485/Modbus
void rs485Initialize();

// Activa o desactiva el modo de depuración
void setRS485Debug(bool debug);