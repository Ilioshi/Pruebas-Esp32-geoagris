#include "otasession.h"

#include <base64.h>

/*
OTA PACKETS

>OTADATA,P,L______________________<
P is packet ID, L is length.
_____ is data.

>OTACURRENTP<
P is current packet ID.
Used to sync packet ID with client.

>OTAFAILEDP<
Failed to write P packet ID.

>OTAOKP<
Packet P succesfully written.

>OTAFINISH<
No more packets left. (client to esp32)

>OTAFINISHP<
OTA succesfull with last packet ID P (esp32 to client)

>OTACORRUPTEDP<
Received data was corrupted.
P is last packet ID.

>OTAFAILEDP<
OTA failed (other error, not corrupted)
P is last packet ID.
*/

OtaSession* OtaSession::begin(BleSerial* ble) {
    OtaSession* session = new OtaSession(ble);

    session->updatePartition = esp_ota_get_next_update_partition(NULL);

    esp_err_t err = esp_ota_begin(session->updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &session->updateHandle);
    if (err != ESP_OK) {
        session->finished = true;
        // Serial.println("Failed to initalize OTA.");
        delete session;
        session = nullptr;
    }
    // else Serial.println("OTA session created.");

    return session; // Will return nullptr if error ocurred.
}

void OtaSession::finish() {
    esp_err_t err = esp_ota_end(updateHandle);

    finished = true;

    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            // Serial.println("Corrupted OTA image received.");
            sendMessage("OTACORRUPTED");
        }
        else {
            // Serial.printf("OTA FAILED (%s)\n", esp_err_to_name(err));
            sendMessage("OTAFAILED");
        }

        abort();

        return;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        // Serial.printf("OTA FAILED (%s)\n", esp_err_to_name(err));
        sendMessage("OTAFAILED");
        
        abort();

        return;
    }


    sendMessage("OTAFINISH");
    // Serial.println("Restarting the device to switch to updated firmware.");
    // ESP.restart();

    delete this;    // Esta línea no es fundamental, ya que antes de limpiar la memoria de este objeto reiniciamos
                    // el dispositivo. La dejo para que si eventualmente se decide cambiar la lógica del reinicio
                    // quede presente y libere la memoria.
                    // CAMBIO 11/2/25: Actualmente el reinicio es manual.
}

void OtaSession::abort() {
    finished = true;
    esp_ota_abort(updateHandle);
}

size_t OtaSession::handlePacket(uint8_t* data, int len) {
    return handlePacket(std::string((char*) data, len));
}

size_t OtaSession::handlePacket(std::string data) {
    int packetStart = data.find(">OTADATA");
    int finishPacket = data.find(">OTAFINISH<");

    size_t parsedLength = 0;
    
    if (finished) return 0;
    
    if (packetStart != -1) {
        size_t idEnd = data.find(',',packetStart+9);
        if (idEnd == std::string::npos) return 0;

        size_t lenEnd = data.find(',', idEnd+1);
        if (lenEnd == std::string::npos) return 0;

        int packetLen;
        int packetID;

        try {
            packetID = std::stoi(data.substr(packetStart+9, idEnd - (packetStart+9)));
            packetLen = std::stoi(data.substr(idEnd+1, lenEnd - (idEnd+1)));   
        } catch (const std::out_of_range& e) {
            sendMessage("OTACORRUPTED");
            return 0;
        }
        
        if (packetID != currentPacketID) {
            sendMessage("OTACURRENT");
            return 0;
        }
        
        data = data.substr(lenEnd+1, packetLen);
        esp_err_t err = esp_ota_write(updateHandle, data.c_str(), data.length());
        if (err != ESP_OK) {
            sendMessage("OTAFAILED");
            return 0;
        }
        
        sendMessage("OTAOK");
        currentPacketID++;

        parsedLength += packetLen + lenEnd - packetStart + 2;
    } 
    else if (finishPacket != -1) {
        finish();
        parsedLength = 12;
    }

    return parsedLength;
}

void OtaSession::sendMessage(const char* message, int packetID) {
    std::string response = ">";
    response.append(message);
    response.append(std::to_string(packetID));
    response.append("<");

    ble->write((uint8_t*)response.c_str(), response.length());
}

std::string get_current_firmware_hash() {
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == nullptr) {
        return std::string("unknown");
    }

    // Create a buffer for the 32-byte SHA256 hash.
    uint8_t sha256[32];
    // Calculate the SHA256 hash for the running partition.
    esp_err_t err = esp_partition_get_sha256(running_partition, sha256);
    if (err != ESP_OK) {
        return std::string("error");
    }

    // Convert the binary hash to a hexadecimal string.
    char hash_str[65];  // 32 bytes * 2 hex digits + 1 null terminator = 65
    for (int i = 0; i < 32; i++) {
        sprintf(&hash_str[i * 2], "%02x", sha256[i]);
    }
    hash_str[64] = '\0'; // Ensure null termination

    return std::string(hash_str);
}