#pragma once
#include <BleSerial.h>
#include <BLEDevice.h>
#include <otasession.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define OTA_HANDSHAKE ">OTA12345678<"

class AgxBle : public BleSerial {
public:
    static AgxBle& getInstance();
    
    void begin(const std::string& version);
    bool busy();
    void reset();
    bool connected();
    void disconnect();
    bool isInitialized();
    
    // Métodos thread-safe
    size_t readBytes(uint8_t* buffer, size_t bufferSize);
    size_t write(const uint8_t* buffer, size_t bufferSize);

    void registerBleDataNotifyTask(TaskHandle_t taskHandle);
    
    std::string getFirmwareVersion();
    
    // BeaconTask
    void setUpScan();
    BLEScanResults* scan();
    
private:
    // static AgxBle* instance;
    static SemaphoreHandle_t mutex;
    
    OtaSession* otaSession;
    unsigned long lastOtaPacket;
    std::string firmwareVersion;
    std::string bleName;
    bool initialized;
    
    // Scan
    BLEScan* pBLEScan;
    BLEScanResults scanResults;
    uint16_t scanInterval;
    uint16_t scanWindow;
    uint32_t scanTime;
    bool activeScan;

    TaskHandle_t dataNotifyTaskHandle;
    
    AgxBle();  // Constructor privado
    ~AgxBle(); // Destructor privado
    
    bool getSemaphore(TickType_t timeout = portMAX_DELAY);
    void releaseSemaphore();
    void initialize();
    void deinitialize();

    void onWrite(BLECharacteristic* pCharacteristic) override;
};