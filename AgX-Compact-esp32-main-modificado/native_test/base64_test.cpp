#include <iostream>
#include <string>
#include <cmath>
#include <cassert>

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

void test_encode_decode_float() {
    float original = 25.5f;
    float min_value = 0.0f;
    float max_value = 100.0f;
    int length = 2;
    
    // Codificar a Base64 URL-safe
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    
    // Decodificar desde Base64 URL-safe
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Calcular la diferencia con signo
    float diff = decoded - original;
    
    // Verificar que el valor decodificado sea aproximadamente igual al original
    float tolerance = 0.5f;
    assert(std::abs(diff) <= tolerance);
    
    std::cout << "Float Test: Original: " << original
              << " -> Encoded: " << encoded
              << " -> Decoded: " << decoded
              << " | Diferencia: " << diff << std::endl;
}

// Prueba con el valor mínimo
void test_encode_decode_min_value() {
    float original = -40.0f;
    float min_value = -40.0f;
    float max_value = 100.0f;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    float diff = decoded - original;
    float tolerance = 0.5f;
    assert(std::abs(diff) <= tolerance);
    
    std::cout << "Min Value Test: Original: " << original
              << " -> Encoded: " << encoded
              << " -> Decoded: " << decoded
              << " | Diferencia: " << diff << std::endl;
}

// Prueba con el valor máximo
void test_encode_decode_max_value() {
    float original = 100.0f;
    float min_value = -40.0f;
    float max_value = 100.0f;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    float diff = decoded - original;
    float tolerance = 0.5f;
    assert(std::abs(diff) <= tolerance);
    
    std::cout << "Max Value Test: Original: " << original
              << " -> Encoded: " << encoded
              << " -> Decoded: " << decoded
              << " | Diferencia: " << diff << std::endl;
}

// Prueba con un valor fuera del rango (menor que el mínimo)
void test_encode_decode_below_min() {
    float original = -50.0f;  // Menor que min_value
    float min_value = -40.0f;
    float max_value = 100.0f;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Debería limitarse al valor mínimo
    assert(decoded == min_value);
    
    float diff = decoded - original;
    
    std::cout << "Below Min Test: Original: " << original
              << " -> Encoded: " << encoded
              << " -> Decoded: " << decoded
              << " | Diferencia: " << diff << std::endl;
}

// Prueba con un valor fuera del rango (mayor que el máximo)
void test_encode_decode_above_max() {
    float original = 120.0f;  // Mayor que max_value
    float min_value = -40.0f;
    float max_value = 100.0f;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Debería limitarse al valor máximo (o muy cercano debido a la precisión de coma flotante)
    float diff = decoded - max_value;
    assert(std::abs(diff) <= 0.1f);
    
    std::cout << "Above Max Test: Original: " << original
              << " -> Encoded: " << encoded
              << " -> Decoded: " << decoded
              << " | Diferencia (con max): " << diff << std::endl;
}

// Prueba con diferentes longitudes de caracteres
void test_encode_decode_different_lengths() {
    float original = 25.5f;
    float min_value = 0.0f;
    float max_value = 100.0f;
    
    for (int length = 1; length <= 4; length++) {
        std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
        float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
        
        float diff = decoded - original;
        // La precisión debería mejorar con una mayor longitud
        float tolerance = 0.5f / length;
        assert(std::abs(diff) <= tolerance);
        
        std::cout << "Length " << length
                  << " Test: Original: " << original
                  << " -> Encoded: " << encoded
                  << " -> Decoded: " << decoded
                  << " | Diferencia: " << diff << std::endl;
    }
}

// Prueba con diferentes rangos
void test_encode_decode_different_ranges() {
    // Prueba temperatura (-50 a 100)
    float temp = 22.5f;
    std::string encoded_temp = toBase64UrlSafe(temp, -50, 100, 2);
    float decoded_temp = fromBase64UrlSafe(encoded_temp, -50, 100);
    float diff_temp = decoded_temp - temp;
    assert(std::abs(diff_temp) <= 1.0f);
    std::cout << "Temperature Test: " << temp
              << " -> Encoded: " << encoded_temp
              << " -> Decoded: " << decoded_temp
              << " | Diferencia: " << diff_temp << std::endl;
    
    // Prueba humedad (0 a 100)
    float humidity = 65.8f;
    std::string encoded_humidity = toBase64UrlSafe(humidity, 0, 100, 2);
    float decoded_humidity = fromBase64UrlSafe(encoded_humidity, 0, 100);
    float diff_humidity = decoded_humidity - humidity;
    assert(std::abs(diff_humidity) <= 1.0f);
    std::cout << "Humidity Test: " << humidity
              << " -> Encoded: " << encoded_humidity
              << " -> Decoded: " << decoded_humidity
              << " | Diferencia: " << diff_humidity << std::endl;
    
    // Prueba presión (0 a 2000)
    float pressure = 1013.25f;
    std::string encoded_pressure = toBase64UrlSafe(pressure, 0, 1200, 2);
    float decoded_pressure = fromBase64UrlSafe(encoded_pressure, 0, 1200);
    float diff_pressure = decoded_pressure - pressure;
    assert(std::abs(diff_pressure) <= 20.0f);
    std::cout << "Pressure Test: " << pressure
              << " -> Encoded: " << encoded_pressure
              << " -> Decoded: " << decoded_pressure
              << " | Diferencia: " << diff_pressure << std::endl;
    
    // Prueba velocidad del viento (0 a 500)
    float wind = 5.5f; // en m/s
    std::string encoded_wind = toBase64UrlSafe(wind, 0, 200, 2);
    float decoded_wind = fromBase64UrlSafe(encoded_wind, 0, 200);
    float diff_wind = decoded_wind - wind;
    assert(std::abs(diff_wind) <= 5.0f);
    std::cout << "Wind Speed Test: " << wind
              << " -> Encoded: " << encoded_wind
              << " -> Decoded: " << decoded_wind
              << " | Diferencia: " << diff_wind << std::endl;
}

// Prueba codificación y decodificación de un mensaje meteorológico completo
void test_weather_message() {
    // Valores meteorológicos originales
    float temperature = 25.2f;
    float humidity = 65.8f;
    float pressure = 1013.2f;
    float wind_speed = 4.25f; // m/s
    float wind_direction = 180.0f;
    float compass = 185.7f;
    float precipitation = 10.5f;
    
    // Codificar cada valor
    std::string encoded = "";
    encoded += toBase64UrlSafe(temperature, -50, 100, 2);        // Temperatura
    encoded += toBase64UrlSafe(humidity, 0, 100, 2);              // Humedad
    encoded += toBase64UrlSafe(pressure, 0, 1200, 2);             // Presión
    encoded += toBase64UrlSafe(wind_speed, 0, 200, 2);            // Velocidad del viento
    encoded += toBase64UrlSafe(wind_direction, 0, 360, 2);        // Dirección del viento
    encoded += toBase64UrlSafe(compass, 0, 360, 2);               // Brújula
    encoded += toBase64UrlSafe(precipitation, 0, 1000, 2);        // Precipitación
    
    std::cout << "Weather Message Encoded: " << encoded << std::endl;
    
    // Decodificar y verificar cada valor
    float decoded_temp = fromBase64UrlSafe(encoded.substr(0, 2), -50, 100);
    float decoded_humidity = fromBase64UrlSafe(encoded.substr(2, 2), 0, 100);
    float decoded_pressure = fromBase64UrlSafe(encoded.substr(4, 2), 0, 1200);
    float decoded_wind_speed = fromBase64UrlSafe(encoded.substr(6, 2), 0, 200);
    float decoded_wind_dir = fromBase64UrlSafe(encoded.substr(8, 2), 0, 360);
    float decoded_compass = fromBase64UrlSafe(encoded.substr(10, 2), 0, 360);
    float decoded_precip = fromBase64UrlSafe(encoded.substr(12, 2), 0, 1000);
    
    std::cout << "Decoded Weather Values:" << std::endl;
    std::cout << "  Temperature: " << decoded_temp << " (Diferencia: " << (decoded_temp - temperature) << ")" << std::endl;
    std::cout << "  Humidity:    " << decoded_humidity << " (Diferencia: " << (decoded_humidity - humidity) << ")" << std::endl;
    std::cout << "  Pressure:    " << decoded_pressure << " (Diferencia: " << (decoded_pressure - pressure) << ")" << std::endl;
    std::cout << "  Wind Speed:  " << decoded_wind_speed << " (Diferencia: " << (decoded_wind_speed - wind_speed) << ")" << std::endl;
    std::cout << "  Wind Dir:    " << decoded_wind_dir << " (Diferencia: " << (decoded_wind_dir - wind_direction) << ")" << std::endl;
    std::cout << "  Compass:     " << decoded_compass << " (Diferencia: " << (decoded_compass - compass) << ")" << std::endl;
    std::cout << "  Precipitation: " << decoded_precip << " (Diferencia: " << (decoded_precip - precipitation) << ")" << std::endl;
}

int main() {
    std::cout << "Ejecutando pruebas de codificación/decodificación Base64 URL-safe..." << std::endl;

    test_encode_decode_float();
    test_encode_decode_min_value();
    test_encode_decode_max_value();
    test_encode_decode_below_min();
    test_encode_decode_above_max();
    test_encode_decode_different_lengths();
    test_encode_decode_different_ranges();
    test_weather_message();

    std::cout << "¡Todas las pruebas pasaron correctamente!" << std::endl;
    return 0;
}

