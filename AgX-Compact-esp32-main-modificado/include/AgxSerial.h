// include/TraxSerial.h
#ifndef AGXSERIAL_H
#define AGXSERIAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define TRAX_TX_PIN 26
#define TRAX_RX_PIN 25
#define TRAX_UART_NUM 2
#define TRAX_BAUD_RATE 115200

class AgxSerial {
    public:
        static AgxSerial& getInstance();
    
        void begin();
        int available();
    
        // Métodos thread-safe que adquieren/liberan automáticamente
        size_t write(const uint8_t* buffer, size_t size);
        size_t read(uint8_t* buffer, size_t size);
        size_t readBytes(uint8_t* buffer, size_t length);
    
    private:
        static AgxSerial* instance;
        static SemaphoreHandle_t mutex;
        HardwareSerial serial;
        bool initialized;
    
        AgxSerial();  // Constructor privado


        bool getSemaphore(TickType_t timeout = portMAX_DELAY);
        void releaseSemaphore();
};

#endif