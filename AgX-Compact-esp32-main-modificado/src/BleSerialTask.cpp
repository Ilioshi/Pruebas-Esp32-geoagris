#ifdef ENABLE_BLE

#include "BleSerialTask.h"
#include "trax_utils.h"
#include "AgxBle.h"
#include "AgxSerial.h"
#include "Messanger_queue.h"

/***********************************************************************************
*                                     Defines                                      *
************************************************************************************/
#ifndef BLE_DEBUG
    #define BLE_DEBUG                       0
#endif

#define BLE_KEEP_ALIVE_INTERVAL_MINUTES     15
#define BLE_INACTIVITY_TIMEOUT_SECONDS      4
#define BLE_SAK_TIMEOUT_HOURS               24

/***********************************************************************************
*                                     Macros                                       *
************************************************************************************/

#define HOUR2MS(ms) ((ms) * 3600000U)
#define MIN2MS(ms) ((ms) * 60000U)
#define SEC2MS(ms) ((ms) * 1000U)

/***********************************************************************************
*                          Helper Functions Prototypes                             *
************************************************************************************/

static void printForDebugBLE(const std::string& message);

/***********************************************************************************
*                                  Main Task                                       *
************************************************************************************/

// Tarea para leer datos del dispositivo y enviarlos a traves de BLE
void bleSerialTask(void* parameter) {

    AgxBle& ble = AgxBle::getInstance();
    AgxSerial& serial = AgxSerial::getInstance();
    
    while(!ble.isInitialized()) {
        vTaskDelay(pdMS_TO_TICKS(100));         // Esperar 100 ms antes de volver a verificar
    }

    uint8_t datable[2048];
    bool communicationEstablished = false;
    bool bleConnected = ble.connected();        // Initial BLE connection state
    unsigned long lastBleDataReceivedTime;        // Last data received from BLE time
    unsigned long lastSerialDataReceivedTime;     // Last data received from Serial2 time
    unsigned long lastSAKReceivedTime = millis(); // Last time >SAK was received
    unsigned long started = millis();
    bool start_informed = false;  
    unsigned long lastKALinformed = millis();
    unsigned long kalCounter = 0;

    while (1)
    {
        // Check BLE connection status and notify if it changes
        bool currentBleConnected = ble.connected();
        if (currentBleConnected != bleConnected)
        {
            bleConnected = currentBleConnected;
            if (bleConnected)
            {
                traxSendReceive(">STX02,e32:connection established " + ble.getFirmwareVersion() + "<");
            }
            else
            {
                traxSendReceive(">STX02,e32:connection lost " + ble.getFirmwareVersion() + "<");
            }
        }


        if(bleConnected) {
            
            #ifdef ENABLE_SIMULATION
                bool queueHadMessages = false;
                // Check queue for data to send first and mark activity in this loop.
                MessangerQueueMessage msg;
                if (MessangerQueue_receiveFromSender(&msg, 0)) {
                    if (msg.len > 0) {
                        ble.write(reinterpret_cast<const uint8_t*>(msg.data), msg.len);
                        lastSerialDataReceivedTime = millis();
                        queueHadMessages = true;
                    }
                }
            #endif

            // Read data from Serial2 and send via BLE
            int len = serial.read(datable, sizeof(datable));
            if (len > 0) {
                #if ENABLE_SIMULATION
                    std::string serialStr((const char*)datable, len);

                    if (!(queueHadMessages && (serialStr.find("RTX15") != std::string::npos))) {
                        ble.write(datable, len);
                    }
                #else
                    ble.write(datable, len);
                #endif
                lastSerialDataReceivedTime = millis(); // Update last data received from Serial2 time
            }
            // Read data from BLE and send via Serial2
            len = ble.readBytes(datable, sizeof(datable));
            if (len > 0) {
                std::string datableStr((const char*)datable, len);
                printForDebugBLE(datableStr);
                lastBleDataReceivedTime = millis(); // Update last data received from BLE time

                serial.write(datable, len);
            
                if (bleConnected && !communicationEstablished)
                {
                    communicationEstablished = true;
                    lastBleDataReceivedTime = millis();    // Start counting from established communication
                    lastSerialDataReceivedTime = millis(); // Start counting from established communication
                    printForDebugBLE("communication started");
                }
            
                // Check if >SAK is received
                if (strstr((char *)datable, ">SAK") != NULL) {
                    lastSAKReceivedTime = millis(); // Update last received >SAK time
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));         // Esperar 10 ms antes de volver a verificar
        
        unsigned long currentTime = millis();

        //Informamos que arranco despues de 5 segundos de haber arrancado (por si el trax necesita tiempo).
        if (!start_informed && (currentTime - started) > 5000)
        {
            traxSendReceive(">STX02,e32:started " + ble.getFirmwareVersion() + "<");
            start_informed = true;
        }    
       
        // >>> Informar Keep Alive cada 15 minutos <<<
        if ((millis() - lastKALinformed) > MIN2MS(BLE_KEEP_ALIVE_INTERVAL_MINUTES))
        {
            kalCounter++;  // incrementa el contador en cada envio

            // Formatea el contador con ceros a la izquierda (6 digitos, por ejemplo)
            char buffer[7];  // 6 digitos + terminador '\0'
            sprintf(buffer, "%06lu", kalCounter);  
            // Construye el string con el numero
            traxSendReceive(">STX02,e32:kal " + std::string(buffer) + "<");
            
            lastKALinformed = millis();
        } 

        // Check if more than 24 hours have passed without receiving >SAK
        if ((currentTime - lastSAKReceivedTime) > HOUR2MS(BLE_SAK_TIMEOUT_HOURS))
        { // 24 hours = 86400000 ms
            traxSendReceive(">STX02,e32:restart no SAK<");
            ESP.restart(); // Keep ESP restart for critical timeout
        }

        
        // If communication is established, check time since last data received
        if (communicationEstablished && !ble.busy()) //OTA: !G_ble.busy()
        {
            // Check if more than 4 seconds have passed without receiving data from BLE
            if ((currentTime - lastBleDataReceivedTime) > SEC2MS(BLE_INACTIVITY_TIMEOUT_SECONDS))
            {
                traxSendReceive(">STX02,e32:ble inactive - Forcing disconnect<");
                ble.disconnect();
                // bleReset(bleName);
                communicationEstablished = false; // Reset communication state
                lastBleDataReceivedTime = millis(); // Reset timer
            }

            // Check if more than 4 seconds have passed without receiving data from Serial2
            if ((currentTime - lastSerialDataReceivedTime) > SEC2MS(BLE_INACTIVITY_TIMEOUT_SECONDS))
            {
                traxSendReceive(">STX02,e32:serial inactive - Forcing disconnect<");
                ble.disconnect();
                // bleReset(bleName);
                communicationEstablished = false; // Reset communication state
                lastSerialDataReceivedTime = millis(); // Reset timer
            }
        }
    }
}


/***********************************************************************************
*                          Helper Functions Definitions                            *
************************************************************************************/

static void printForDebugBLE(const std::string& message) {
    #if BLE_DEBUG
        Serial.printf("[BLE] %s\n", message.c_str());
    #endif
}


#endif // ENABLE_BLE


// >RTX15,060226144206;3;Do not spray;3;12.9;Very High;304;1;27.2;0k;21;2;1;Very Low;N;22;0;0;<
// >STX15,060226144052;3;Do not spray;3;12.9;Very High;305;1;27.2;ok;21;2;1;Very Low;N;22;0;0;<