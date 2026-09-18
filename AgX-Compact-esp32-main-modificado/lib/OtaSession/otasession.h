#pragma once

#include "BleSerial.h"
#include "esp_ota_ops.h"

#define OTA_TRANSFER_SIZE 480

class OtaSession
{
private:
    bool finished = false;
    int currentPacketID = 0;

    BleSerial* ble;

    const esp_partition_t* updatePartition;
    esp_ota_handle_t updateHandle;

    void sendMessage(const char* message, int packetID);
    void sendMessage(const char* message) { sendMessage(message, currentPacketID); }
    void finish();

    OtaSession(BleSerial* _ble): ble(_ble) {}
public:
    void abort();
    size_t handlePacket(uint8_t* data, int len);
    size_t handlePacket(std::string data);
    bool isActive() { return !this->finished; }
    
    static OtaSession* begin(BleSerial* _ble);
};

std::string get_current_firmware_hash();