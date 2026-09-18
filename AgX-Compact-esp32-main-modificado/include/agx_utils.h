#pragma once
#include <Arduino.h>
#include <string>

// Verifica si una cadena contiene solo caracteres numéricos
bool esNumerico(const std::string& str);

// Convierte un valor a formato hexadecimal de 2 bytes (uint16_t)
// Escala automáticamente el valor entre minValue y maxValue al rango 0-65535
// Para valores fuera del rango, se limita al mínimo o máximo
uint16_t convertToHex16(float value, float minValue, float maxValue);

// Convierte un valor a formato hexadecimal de 1 byte (uint8_t)
// Escala automáticamente el valor entre minValue y maxValue al rango 0-255
// Para valores fuera del rango, se limita al mínimo o máximo
uint8_t convertToHex8(float value, float minValue, float maxValue);

// Convierte un valor entero sin signo a un valor float
// Realiza el proceso inverso a convertToHex16/convertToHex8
// value: valor entero sin signo (uint16_t o uint8_t)
// minValue, maxValue: rango para escalar el valor
// bits: número de bits (8 para uint8_t, 16 para uint16_t)
float fromUnsignedInt(uint32_t value, float minValue, float maxValue, int bits = 16);

// Convierte una cadena hexadecimal a un valor float
// hexStr: la cadena hexadecimal (ej: "A4F1")
// minValue, maxValue: rango para escalar el valor
// bits: número de bits (8 para uint8_t, 16 para uint16_t)
float fromHexString(const std::string& hexStr, float minValue, float maxValue, int bits = 16);

// Codifica un valor float en una cadena de caracteres base64 URL-safe
// Escala el valor entre minValue y maxValue y lo codifica con el alfabeto base64 URL-safe
// length: longitud del string resultante en caracteres
std::string toBase64UrlSafe(float value, float minValue, float maxValue, int length);

// Decodifica una cadena de caracteres base64 URL-safe y devuelve un valor float
// Convierte la cadena codificada en un valor float entre minValue y maxValue
// Revierte el proceso de toBase64UrlSafe
float fromBase64UrlSafe(const std::string& base64Str, float minValue, float maxValue);