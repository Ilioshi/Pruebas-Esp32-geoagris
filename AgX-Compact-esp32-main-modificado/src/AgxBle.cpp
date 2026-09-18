#include "AgxBle.h"
#include "trax_utils.h"
#include "BleSerialServer.h"


#define DEFAULT_WINDOW_TIME 99
#define DEFAULT_INTERVAL_TIME 100

#define DEFAULT_SCAN_TIME_S 1
#define ACTIVE_SCAN false


// AgxBle* AgxBle::instance = nullptr;
SemaphoreHandle_t AgxBle::mutex = nullptr;

AgxBle::AgxBle() {
    firmwareVersion = "";
    otaSession = nullptr;
    pBLEScan = nullptr;
    initialized = false;
    lastOtaPacket = 0;
    scanInterval = DEFAULT_INTERVAL_TIME;
    scanWindow = DEFAULT_WINDOW_TIME;
    scanTime = DEFAULT_SCAN_TIME_S;
    activeScan = ACTIVE_SCAN;
    dataNotifyTaskHandle = nullptr;
    
    if (mutex == nullptr) {
        mutex = xSemaphoreCreateMutex();
    }
}

AgxBle::~AgxBle() {
    if (getSemaphore()) {
        deinitialize();
        if (otaSession) {
            otaSession->abort();
            delete otaSession;
            otaSession = nullptr;
        }
        releaseSemaphore();
    }
}

AgxBle& AgxBle::getInstance() {
    static AgxBle instance;
    return instance;
    // if (instance == nullptr) {
    //     instance = new AgxBle();
    // }
    // return *instance;
}

void AgxBle::begin(const std::string& version) {
    // Solo inicializo si no estaba inicializado previamente
    if (!isInitialized()) {
        firmwareVersion = version;
        
        // Obtener el nombre de la maquina usando la funcion de trax_utils
        std::string machineName = traxGetMachineName();
        
        // Si el nombre es "unknown", esperar 5 segundos y volver a intentar una sola vez
        if (machineName == "unknown") {
            delay(5000);
            machineName = traxGetMachineName();
        }

        // Crear el nombre completo para la conexion BT: "AgX nombre_maquina versionNumber"
        bleName = "AgX " + machineName + " " + firmwareVersion;

        // HACK: Libero el stack de Bluetooth Clasico
        // Source: https://sourcevu.sysprogs.com/espressif/esp-idf/symbols/esp_bt_mem_release
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        traxSendReceive(">STX02,e32:bleInitialize " + bleName + "<");

        if (getSemaphore()) {
            initialize();
            releaseSemaphore();
        }
    }
}

void AgxBle::registerBleDataNotifyTask(TaskHandle_t taskHandle) {
    if (getSemaphore()) {
        dataNotifyTaskHandle = taskHandle;
        releaseSemaphore();
    }
}

bool AgxBle::busy() {
    if (getSemaphore()) {
        bool isBusy = (otaSession != nullptr && otaSession->isActive());
        releaseSemaphore();
        return isBusy;
    }
    return false;
}

void AgxBle::reset() {
    if (getSemaphore()) {
        deinitialize();
        initialize();
        releaseSemaphore();
    }
    traxSendReceive(">STX02,e32:ble reset<");
}

bool AgxBle::connected() {
    bool isConnected = false;
    if (getSemaphore()) {
        isConnected = BleSerial::connected();
        releaseSemaphore();
    }
    return isConnected;
}

void AgxBle::disconnect() {
    if (getSemaphore()) {
        auto* server = BleSerialServer::getInstance().Server;
        if (server) {
            server->disconnect(server->getConnId());
        }        
        releaseSemaphore();
    }
}

bool AgxBle::isInitialized() {
    bool isInit = false;
    if (getSemaphore()) {
        isInit = initialized && BleSerial::isStarted();
        releaseSemaphore();
    }
    return isInit;
}

size_t AgxBle::readBytes(uint8_t* buffer, size_t bufferSize) {

    size_t len = 0; // Definicion para proteger
    
    if (getSemaphore()) {
        len = BleSerial::readBytes(buffer, bufferSize);
        releaseSemaphore();
    }

    if (len != 0) {
        std::string packet((char*)buffer, len);
        
        unsigned long currentTime = millis();
        
        if (otaSession && (currentTime - lastOtaPacket > 10000 || !otaSession->isActive())) {
            otaSession->abort();
            delete otaSession;
            otaSession = nullptr;
        }
        
        // IMPORTANTE: este if (el que procesa OTADATA) tiene que ser EL PRIMERO, ya que borra el paquete de data al procesarlo.
        // esto es relevante ya que dentro del binario estan los strings de control definidos, por lo que al enviarlos por OTA serian
        // parseado como paquetes de control y no como data de OTA. Este problema se evita ya que 
        if (otaSession && otaSession->isActive()) {
            size_t otaPosition;
            while ((otaPosition = packet.find(">OTA")) != std::string::npos) {
                // size_t otaPacketLength = (packet[otaPosition + 4] == 'D') ? (14 + OTA_TRANSFER_SIZE) : (packet.find('<', otaPosition) - otaPosition + 1);
                // If packet has 'D' (>OTADATA...<) fixed packet length
                // Else dynamically find packet length
                
                size_t otaPacketLength = otaSession->handlePacket(packet.substr(otaPosition));
                
                //ESP_LOGI("OTA", "Erasing %s", packet.substr(otaPosition, otaPacketLength));
                packet.erase(otaPosition, otaPacketLength);
                
                lastOtaPacket = millis();
            }
            
            packet.copy((char*)buffer, packet.length());
            len = packet.length();
            //ESP_LOGI("OTA", "Resulting packet: %s", packet);
        }
        
        if (packet.find(OTA_HANDSHAKE) != std::string::npos) {
            if (otaSession) {
                otaSession->abort();
                delete otaSession;
            }
            
            otaSession = OtaSession::begin(this);
            if (otaSession) {
                write((uint8_t*)OTA_HANDSHAKE, strlen(OTA_HANDSHAKE));
            }
            
            lastOtaPacket = millis();
        }
        
        if (packet.find(">ESPRESTART<") != std::string::npos) {
            releaseSemaphore();
            ESP.restart();
        }
        
        if (packet.find(">ESPFIRMWAREHASH<") != std::string::npos) {
            auto hash = ">ESPFIRMWAREHASH," + get_current_firmware_hash() + "<";
            write((uint8_t*)hash.c_str(), hash.size());
        }
    }
    
    releaseSemaphore();     // Esto es por las dudas
    return len;
}

size_t AgxBle::write(const uint8_t* buffer, size_t bufferSize) {
    size_t written = 0;
    if (getSemaphore()) {
        written = BleSerial::write(buffer, bufferSize);
        releaseSemaphore();
    }
    return written;
}

std::string AgxBle::getFirmwareVersion() {
    return firmwareVersion;
}

void AgxBle::setUpScan() {
    if (pBLEScan == nullptr) {
        if (getSemaphore()) {
            pBLEScan = BLEDevice::getScan(); // create new scan
            releaseSemaphore();
        }
    }
    
    if (pBLEScan != nullptr) {
        pBLEScan->setActiveScan(activeScan);
        pBLEScan->setInterval(scanInterval);
        pBLEScan->setWindow(scanWindow);
    }
}

BLEScanResults* AgxBle::scan() {
    if (pBLEScan == nullptr || !getSemaphore()) {
        return nullptr;
    }
    
    pBLEScan->start(scanTime, false); // el false es para borrar datos anteriores
    
    scanResults = pBLEScan->getResults();
    releaseSemaphore();
    
    return &scanResults;
}

/****************************************
*            Metodos privados           *
*****************************************/

bool AgxBle::getSemaphore(TickType_t timeout) {
    return mutex != nullptr && xSemaphoreTake(mutex, timeout) == pdTRUE;
}

void AgxBle::releaseSemaphore() {
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

void AgxBle::initialize() {
    BleSerial::begin(bleName.c_str()); // Inicializa el BLE con el nuevo nombre
    BleSerial::setTimeout(100);
    initialized = true;
}

void AgxBle::deinitialize() {
    BleSerial::end();
    initialized = false;
}

void AgxBle::onWrite(BLECharacteristic* pCharacteristic) {
    BleSerial::onWrite(pCharacteristic);

    if (dataNotifyTaskHandle != nullptr) {
        xTaskNotifyGive(dataNotifyTaskHandle);
    }
}
