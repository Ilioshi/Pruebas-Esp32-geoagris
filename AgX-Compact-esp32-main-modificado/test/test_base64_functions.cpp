#include <Arduino.h>
#include <unity.h>
#include "agx_utils.h"

void setUp(void) {
    // Configuración antes de cada prueba
}

void tearDown(void) {
    // Limpieza después de cada prueba
}

// Prueba codificación y decodificación de un valor simple
void test_encode_decode_float() {
    float original = 25.5;
    float min_value = 0.0;
    float max_value = 100.0;
    int length = 2;
    
    // Codificar a base64 URL-safe
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    
    // Decodificar de base64 URL-safe
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Verificar que el valor decodificado sea aproximadamente igual al original
    // Permitimos una pequeña diferencia debido a la precisión limitada
    TEST_ASSERT_FLOAT_WITHIN(0.5, original, decoded);
    
    // Imprimir información para depuración
    Serial.print("Original: ");
    Serial.print(original);
    Serial.print(" -> Encoded: ");
    Serial.print(encoded.c_str());
    Serial.print(" -> Decoded: ");
    Serial.println(decoded);
}

// Prueba con el valor mínimo
void test_encode_decode_min_value() {
    float original = -40.0;
    float min_value = -40.0;
    float max_value = 100.0;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    TEST_ASSERT_FLOAT_WITHIN(0.5, original, decoded);
    
    Serial.print("Min value test - Original: ");
    Serial.print(original);
    Serial.print(" -> Encoded: ");
    Serial.print(encoded.c_str());
    Serial.print(" -> Decoded: ");
    Serial.println(decoded);
}

// Prueba con el valor máximo
void test_encode_decode_max_value() {
    float original = 100.0;
    float min_value = -40.0;
    float max_value = 100.0;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    TEST_ASSERT_FLOAT_WITHIN(0.5, original, decoded);
    
    Serial.print("Max value test - Original: ");
    Serial.print(original);
    Serial.print(" -> Encoded: ");
    Serial.print(encoded.c_str());
    Serial.print(" -> Decoded: ");
    Serial.println(decoded);
}

// Prueba con un valor fuera del rango (menor que el mínimo)
void test_encode_decode_below_min() {
    float original = -50.0;  // Menor que min_value
    float min_value = -40.0;
    float max_value = 100.0;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Debería ser limitado al valor mínimo
    TEST_ASSERT_EQUAL_FLOAT(min_value, decoded);
    
    Serial.print("Below min test - Original: ");
    Serial.print(original);
    Serial.print(" -> Encoded: ");
    Serial.print(encoded.c_str());
    Serial.print(" -> Decoded: ");
    Serial.println(decoded);
}

// Prueba con un valor fuera del rango (mayor que el máximo)
void test_encode_decode_above_max() {
    float original = 120.0;  // Mayor que max_value
    float min_value = -40.0;
    float max_value = 100.0;
    int length = 2;
    
    std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
    float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
    
    // Debería ser limitado al valor máximo
    TEST_ASSERT_EQUAL_FLOAT(max_value, decoded);
    
    Serial.print("Above max test - Original: ");
    Serial.print(original);
    Serial.print(" -> Encoded: ");
    Serial.print(encoded.c_str());
    Serial.print(" -> Decoded: ");
    Serial.println(decoded);
}

// Prueba con diferentes longitudes de caracteres
void test_encode_decode_different_lengths() {
    float original = 25.5;
    float min_value = 0.0;
    float max_value = 100.0;
    
    for (int length = 1; length <= 4; length++) {
        std::string encoded = toBase64UrlSafe(original, min_value, max_value, length);
        float decoded = fromBase64UrlSafe(encoded, min_value, max_value);
        
        // La precisión debería mejorar con una mayor longitud
        float tolerance = 0.5 / length;
        TEST_ASSERT_FLOAT_WITHIN(tolerance, original, decoded);
        
        Serial.print("Length ");
        Serial.print(length);
        Serial.print(" test - Original: ");
        Serial.print(original);
        Serial.print(" -> Encoded: ");
        Serial.print(encoded.c_str());
        Serial.print(" -> Decoded: ");
        Serial.println(decoded);
    }
}

// Prueba con diferentes rangos
void test_encode_decode_different_ranges() {
    // Prueba temperatura (-50 a 100)
    float temp = 22.5;
    std::string encoded_temp = toBase64UrlSafe(temp, -50, 100, 2);
    float decoded_temp = fromBase64UrlSafe(encoded_temp, -50, 100);
    TEST_ASSERT_FLOAT_WITHIN(1.0, temp, decoded_temp);
    Serial.print("Temperature test: ");
    Serial.print(temp);
    Serial.print(" -> ");
    Serial.print(encoded_temp.c_str());
    Serial.print(" -> ");
    Serial.println(decoded_temp);
    
    // Prueba humedad (0 a 100)
    float humidity = 65.8;
    std::string encoded_humidity = toBase64UrlSafe(humidity, 0, 100, 2);
    float decoded_humidity = fromBase64UrlSafe(encoded_humidity, 0, 100);
    TEST_ASSERT_FLOAT_WITHIN(1.0, humidity, decoded_humidity);
    Serial.print("Humidity test: ");
    Serial.print(humidity);
    Serial.print(" -> ");
    Serial.print(encoded_humidity.c_str());
    Serial.print(" -> ");
    Serial.println(decoded_humidity);
    
    // Prueba presión (0 a 2000)
    float pressure = 1013.25;
    std::string encoded_pressure = toBase64UrlSafe(pressure, 0, 2000, 2);
    float decoded_pressure = fromBase64UrlSafe(encoded_pressure, 0, 2000);
    TEST_ASSERT_FLOAT_WITHIN(20.0, pressure, decoded_pressure);
    Serial.print("Pressure test: ");
    Serial.print(pressure);
    Serial.print(" -> ");
    Serial.print(encoded_pressure.c_str());
    Serial.print(" -> ");
    Serial.println(decoded_pressure);
    
    // Prueba velocidad del viento (0 a 500)
    float wind = 5.5; // en m/s
    std::string encoded_wind = toBase64UrlSafe(wind, 0, 500, 2);
    float decoded_wind = fromBase64UrlSafe(encoded_wind, 0, 500);
    TEST_ASSERT_FLOAT_WITHIN(5.0, wind, decoded_wind);
    Serial.print("Wind speed test: ");
    Serial.print(wind);
    Serial.print(" -> ");
    Serial.print(encoded_wind.c_str());
    Serial.print(" -> ");
    Serial.println(decoded_wind);
}

// Prueba codificación y decodificación de un mensaje meteorológico completo
void test_weather_message() {
    // Valores meteorológicos originales
    float temperature = 25.2;
    float humidity = 65.8;
    float pressure = 1013.2;
    float wind_speed = 4.25; // m/s
    float wind_direction = 180.0;
    float compass = 185.7;
    float precipitation = 10.5;
    
    // Codificar cada valor
    std::string encoded = "";
    encoded += toBase64UrlSafe(temperature, -50, 100, 2);        // Temperatura
    encoded += toBase64UrlSafe(humidity, 0, 100, 2);            // Humedad
    encoded += toBase64UrlSafe(pressure, 0, 2000, 2);           // Presión
    encoded += toBase64UrlSafe(wind_speed, 0, 500, 2);          // Velocidad del viento
    encoded += toBase64UrlSafe(wind_direction, 0, 360, 2);      // Dirección del viento
    encoded += toBase64UrlSafe(compass, 0, 360, 2);             // Brújula
    encoded += toBase64UrlSafe(precipitation, 0, 4095, 2);      // Precipitación
    
    Serial.print("Weather message: ");
    Serial.println(encoded.c_str());
    
    // Decodificar y verificar cada valor
    float decoded_temp = fromBase64UrlSafe(encoded.substr(0, 2), -50, 100);
    float decoded_humidity = fromBase64UrlSafe(encoded.substr(2, 2), 0, 100);
    float decoded_pressure = fromBase64UrlSafe(encoded.substr(4, 2), 0, 2000);
    float decoded_wind_speed = fromBase64UrlSafe(encoded.substr(6, 2), 0, 500);
    float decoded_wind_dir = fromBase64UrlSafe(encoded.substr(8, 2), 0, 360);
    float decoded_compass = fromBase64UrlSafe(encoded.substr(10, 2), 0, 360);
    float decoded_precip = fromBase64UrlSafe(encoded.substr(12, 2), 0, 4095);
    
    TEST_ASSERT_FLOAT_WITHIN(2.0, temperature, decoded_temp);
    TEST_ASSERT_FLOAT_WITHIN(2.0, humidity, decoded_humidity);
    TEST_ASSERT_FLOAT_WITHIN(20.0, pressure, decoded_pressure);
    TEST_ASSERT_FLOAT_WITHIN(5.0, wind_speed, decoded_wind_speed);
    TEST_ASSERT_FLOAT_WITHIN(5.0, wind_direction, decoded_wind_dir);
    TEST_ASSERT_FLOAT_WITHIN(5.0, compass, decoded_compass);
    TEST_ASSERT_FLOAT_WITHIN(40.0, precipitation, decoded_precip);
    
    Serial.println("Decoded values:");
    Serial.print("Temp: "); Serial.println(decoded_temp);
    Serial.print("Humidity: "); Serial.println(decoded_humidity);
    Serial.print("Pressure: "); Serial.println(decoded_pressure);
    Serial.print("Wind Speed: "); Serial.println(decoded_wind_speed);
    Serial.print("Wind Direction: "); Serial.println(decoded_wind_dir);
    Serial.print("Compass: "); Serial.println(decoded_compass);
    Serial.print("Precipitation: "); Serial.println(decoded_precip);
}

void setup() {
    // Inicializar la comunicación serial
    Serial.begin(115200);
    
    // Esperar a que el puerto serial esté disponible
    delay(2000);
    
    UNITY_BEGIN();
    
    // Ejecutar pruebas
    RUN_TEST(test_encode_decode_float);
    RUN_TEST(test_encode_decode_min_value);
    RUN_TEST(test_encode_decode_max_value);
    RUN_TEST(test_encode_decode_below_min);
    RUN_TEST(test_encode_decode_above_max);
    RUN_TEST(test_encode_decode_different_lengths);
    RUN_TEST(test_encode_decode_different_ranges);
    RUN_TEST(test_weather_message);
    
    UNITY_END();
}

void loop() {
    // No hacer nada en el bucle principal
}