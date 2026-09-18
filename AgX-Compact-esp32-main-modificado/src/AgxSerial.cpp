#include "AgxSerial.h"

AgxSerial* AgxSerial::instance = nullptr;
SemaphoreHandle_t AgxSerial::mutex = nullptr;

AgxSerial::AgxSerial() : serial(TRAX_UART_NUM), initialized(false) {
    if (mutex == nullptr) {
        mutex = xSemaphoreCreateMutex();
    }
}

AgxSerial& AgxSerial::getInstance() {
    if (instance == nullptr) {
        instance = new AgxSerial();
    }
    return *instance;
}

void AgxSerial::begin() {
    if (getSemaphore()) {
        if (!initialized) {
            serial.begin(TRAX_BAUD_RATE, SERIAL_8N1, TRAX_RX_PIN, TRAX_TX_PIN);
            initialized = true;
        }
        releaseSemaphore();
    }
}

size_t AgxSerial::write(const uint8_t* buffer, size_t size) {
    if (getSemaphore(portMAX_DELAY)) {
        size_t written = serial.write(buffer, size);
        releaseSemaphore();
        return written;
    }
    return 0;
}

size_t AgxSerial::read(uint8_t* buffer, size_t size) {
    if (getSemaphore(portMAX_DELAY)) {
        size_t read = serial.read(buffer, size);
        releaseSemaphore();
        return read;
    }
    return 0;
}

size_t AgxSerial::readBytes(uint8_t* buffer, size_t length) {
    if (getSemaphore(portMAX_DELAY)) {
        size_t bytesRead = serial.readBytes(buffer, length);
        releaseSemaphore();
        return bytesRead;
    }
    return 0;
}

int AgxSerial::available() {
    if (getSemaphore(portMAX_DELAY)) {
        int avail = serial.available();
        releaseSemaphore();
        return avail;
    }
    return 0;
}

// Métodos privados

bool AgxSerial::getSemaphore(TickType_t timeout) {
    return mutex != nullptr && xSemaphoreTake(mutex, timeout) == pdTRUE;
}

void AgxSerial::releaseSemaphore() {
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}