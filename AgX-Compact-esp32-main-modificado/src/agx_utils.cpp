#include "agx_utils.h"      // TODO: revisar la utilidad de este archivo
#include <cmath>

// Función para verificar si una cadena es numérica
bool esNumerico(const std::string& str)
{
    if (str.empty())
    {
        return false; // Retorna false si la cadena es vacía
    }
    for (char c : str)
    {
        if (!isdigit(c) && c != '.' && c != '-')
        {
            return false;
        }
    }
    return true;
}

// Convierte un valor a formato hexadecimal de 2 bytes (uint16_t)
// Escala automáticamente el valor entre minValue y maxValue al rango 0-65535
uint16_t convertToHex16(float value, float minValue, float maxValue)
{
    // Verificar si el valor está fuera de los límites
    if (value < minValue) {
        return 0; // Valor mínimo
    }
    if (value > maxValue) {
        return 0xFFFF; // Valor máximo (65535)
    }
    
    // Calcular el factor de escala y offset
    float range = maxValue - minValue;
    float normalizedValue = (value - minValue) / range; // Valor entre 0 y 1
    
    // Escalar al rango de uint16_t (0-65535)
    uint16_t hexValue = (uint16_t)(normalizedValue * 65535);
    
    return hexValue;
}

// Convierte un valor a formato hexadecimal de 1 byte (uint8_t)
// Escala automáticamente el valor entre minValue y maxValue al rango 0-255
uint8_t convertToHex8(float value, float minValue, float maxValue)
{
    // Verificar si el valor está fuera de los límites
    if (value < minValue) {
        return 0; // Valor mínimo
    }
    if (value > maxValue) {
        return 0xFF; // Valor máximo (255)
    }
    
    // Calcular el factor de escala y offset
    float range = maxValue - minValue;
    float normalizedValue = (value - minValue) / range; // Valor entre 0 y 1
    
    // Escalar al rango de uint8_t (0-255)
    uint8_t hexValue = (uint8_t)(normalizedValue * 255);
    
    return hexValue;
}

// Convierte un valor entero sin signo a un valor float
float fromUnsignedInt(uint32_t value, float minValue, float maxValue, int bits)
{
    // Validamos que 'bits' esté dentro de un rango aceptable (1 a 32 bits para uint32_t)
    if (bits <= 0 || bits > 32) {
        // Podríamos manejar un error aquí; por simplicidad, devolvemos minValue
        return minValue;
    }
    
    // Calcular el valor máximo (maxInt) representable con 'bits' bits: 2^bits - 1
    uint32_t maxInt = (bits == 32) ? 0xFFFFFFFF : ((1U << bits) - 1);
    
    // Asegurarse de que 'value' no exceda el máximo representable
    if (value > maxInt) {
        value = maxInt;
    }
    
    // Normalizar el valor a un rango de 0 a 1
    float normalizedValue = static_cast<float>(value) / static_cast<float>(maxInt);
    
    // Escalar el valor normalizado al rango deseado [minValue, maxValue]
    float result = minValue + normalizedValue * (maxValue - minValue);
    
    return result;
}


// Convierte una cadena hexadecimal a un valor float
float fromHexString(const std::string& hexStr, float minValue, float maxValue, int bits)
{
    // Si la cadena está vacía, devolver el valor mínimo
    if (hexStr.empty()) {
        return minValue;
    }
    
    // Convertir la cadena hexadecimal a un valor entero
    uint32_t hexValue = 0;
    try {
        // Usar std::stoul para convertir de string a unsigned long
        hexValue = std::stoul(hexStr, nullptr, 16);
    } catch (...) {
        // Si hay un error en la conversión, devolver el valor mínimo
        return minValue;
    }
    
    // Usar la función fromUnsignedInt para convertir el valor entero a float
    return fromUnsignedInt(hexValue, minValue, maxValue, bits);
}

// Codifica un valor float en una cadena de caracteres Base64 URL-safe (sin padding)
// usando redondeo mediante conversión directa a un entero en [0, 64^length - 1].
std::string toBase64UrlSafe(float value, float minValue, float maxValue, int length)
{
    // Alfabeto base64 URL-safe (según RFC 4648)
    static const char base64UrlChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    // Normaliza el valor en el rango [0, 1]
    float fraction;
    if (value <= minValue) {
        fraction = 0.0f;
    } else if (value >= maxValue) {
        fraction = 1.0f;
    } else {
        fraction = (value - minValue) / (maxValue - minValue);
    }
    
    // Calcula el máximo entero representable con la longitud dada
    int maxInt = static_cast<int>(std::pow(64, length)) - 1;
    // Calcula el valor entero redondeado
    int valueInt = static_cast<int>(std::round(fraction * maxInt));
    
    // Convierte el entero a una cadena base64 de longitud 'length'
    std::string result(length, 'A');  // se inicializa con caracteres por defecto
    for (int i = 0; i < length; i++) {
        int power = static_cast<int>(std::pow(64, length - i - 1));
        int digit = valueInt / power;
        valueInt %= power;
        // Se asegura que el dígito esté en [0, 63]
        if (digit < 0) digit = 0;
        if (digit > 63) digit = 63;
        result[i] = base64UrlChars[digit];
    }
    return result;
}

// Decodifica una cadena Base64 URL-safe y devuelve un valor float en [minValue, maxValue].
float fromBase64UrlSafe(const std::string& base64Str, float minValue, float maxValue)
{
    static const char base64UrlChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static int charMap[256];
    static bool mapInitialized = false;

    if (!mapInitialized) {
        for (int i = 0; i < 256; i++)
            charMap[i] = -1;
        for (int i = 0; i < 64; i++) {
            unsigned char c = static_cast<unsigned char>(base64UrlChars[i]);
            charMap[c] = i;
        }
        mapInitialized = true;
    }

    double value64 = 0.0;
    int len = static_cast<int>(base64Str.size());
    for (int i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(base64Str[i]);
        int index = (c < 256) ? charMap[c] : -1;
        if (index < 0) index = 0;
        value64 = value64 * 64.0 + index;
    }

    double fraction = (len > 0) ? (value64 / std::pow(64.0, len)) : 0.0;
    float decodedValue = static_cast<float>(minValue + (maxValue - minValue) * fraction);
    
    if (decodedValue < minValue)
        decodedValue = minValue;
    else if (decodedValue > maxValue)
        decodedValue = maxValue;
    
    return decodedValue;
}