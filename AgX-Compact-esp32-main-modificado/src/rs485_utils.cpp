#include "rs485_utils.h"

// Pines UART estacion
#define G_RS485_TX_PIN 17   // Pin TX del ESP32 conectado al DI del MAX485
#define G_RS485_RX_PIN 16   // Pin RX del ESP32 conectado al RO del MAX485
#define G_RS485_DE_RE_PIN 4 // Pin GPIO para controlar DE y RE del MAX485

static HardwareSerial G_RS485Serial(1); // Usando el puerto UART1
static bool G_debug_rs485 = false;
static bool G_rs485_initialized = false;

// Variable estática para almacenar el handle del semáforo, inicialmente NULL.
static SemaphoreHandle_t G_semaphoreRS485 = NULL;

// Función interna que retorna el semáforo, creándolo si es necesario.
static SemaphoreHandle_t getSemaphoreRS485(void) {
    if (G_semaphoreRS485 == NULL) {
        G_semaphoreRS485 = xSemaphoreCreateBinary();
        xSemaphoreGive(G_semaphoreRS485);
    }
    return G_semaphoreRS485;
}

// Función para imprimir mensajes de depuración (static, solo usada dentro de este archivo)
static void printForDebugRS485(const std::string& message) {
    if (G_debug_rs485) {
        std::string msg = message;
        size_t pos;
        // Reemplazar '>' con '}'
        while ((pos = msg.find('>')) != std::string::npos) {
            msg.replace(pos, 1, "}");
        }
        // Reemplazar '<' con '{'
        while ((pos = msg.find('<')) != std::string::npos) {
            msg.replace(pos, 1, "{");
        }
        Serial.println(msg.c_str());
    }
}


// Inicializa los pines y configuración para RS485/Modbus
void rs485Initialize() {
    if (!G_rs485_initialized) {
        G_RS485Serial.begin(9600, SERIAL_8N1, G_RS485_RX_PIN, G_RS485_TX_PIN); // UART estacion
        pinMode(G_RS485_DE_RE_PIN, OUTPUT); // Configurar pin lectura escritura modbus
        G_rs485_initialized = true;
        printForDebugRS485(">RS485:initialized<");
    }
}

// Activa o desactiva el modo de depuración
void setRS485Debug(bool debug) {
    G_debug_rs485 = debug;
}

// Función para calcular CRC-16 Modbus
uint16_t ModRTU_CRC(uint8_t *buf, int len) {
    uint16_t crc = 0xFFFF;

    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos]; // XOR byte into least sig. byte of crc
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {         // Si el LSB es 1
                crc >>= 1;     // Desplazar a la derecha
                crc ^= 0xA001; // XOR con el polinomio
            } else {
                crc >>= 1; // Solo desplazar
            }
        }
    }
    return crc;
}

// Función para enviar una consulta Modbus. Coloca la respuesta en *responseReceived. Verifica la respuesta, retorna True si es correcta 
bool rs485ModbusAsk(uint8_t slaveAddress, uint16_t firstRegister, uint16_t numRegisters, uint8_t *responseReceived) {
    const int MAX_RESPONSE_SIZE = 256;
    bool success = false;

    rs485Initialize();

    // Construir el mensaje Modbus RTU para lectura de registros (función 0x03)
    // - Dirección del esclavo (1 byte)
    // - Código de función (1 byte) - 0x03 para lectura de registros
    // - Dirección del primer registro (2 bytes, MSB primero)
    // - Cantidad de registros a leer (2 bytes, MSB primero)
    // - CRC (2 bytes, LSB primero)

    uint8_t messageToSend[8];
    messageToSend[0] = slaveAddress;
    messageToSend[1] = 0x03;
    messageToSend[2] = (firstRegister >> 8) & 0xFF;
    messageToSend[3] = firstRegister & 0xFF;
    messageToSend[4] = (numRegisters >> 8) & 0xFF;
    messageToSend[5] = numRegisters & 0xFF;
    
    // Calcular y añadir el CRC
    uint16_t crc = ModRTU_CRC(messageToSend, 6);
    messageToSend[6] = crc & 0xFF;
    messageToSend[7] = (crc >> 8) & 0xFF;

    // Conseguir el semáforo para evitar colisiones
    SemaphoreHandle_t sem = getSemaphoreRS485();

    // Toma el semaforo
    if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE) {
        digitalWrite(G_RS485_DE_RE_PIN, HIGH);
        G_RS485Serial.write(messageToSend, 8);
        G_RS485Serial.flush();
        digitalWrite(G_RS485_DE_RE_PIN, LOW);

        unsigned long startTime = millis();
        const unsigned long timeout = 200;
        uint8_t response[MAX_RESPONSE_SIZE];
        int bytesRead = 0;

        while ((millis() - startTime) < timeout) {
            int availableBytes = G_RS485Serial.available();
            if (availableBytes > 0) {
                int bytesToRead = min(availableBytes, MAX_RESPONSE_SIZE - bytesRead);
                bytesRead += G_RS485Serial.readBytes(&response[bytesRead], bytesToRead);
            } else {
                delay(10);
            }
        }

        // ** FILTRO: Verificar si el primer byte es 0x00 y ajustarlo**
        if (bytesRead > 1 && response[0] == 0x00 && response[1] == slaveAddress) {
            // **Desplazar la respuesta una posición a la izquierda**
            for (int i = 0; i < bytesRead - 1; i++) {
                response[i] = response[i + 1];
            }
            bytesRead--;  // Ajustar la cantidad de bytes recibidos
        }

        // Verificar que haya suficientes bytes para una respuesta válida
        if (bytesRead >= 5) {
            uint16_t crcCalculated = ModRTU_CRC(response, bytesRead - 2);
            uint16_t crcReceived = (response[bytesRead - 1] << 8) | response[bytesRead - 2];

            // Verificar CRC y dirección correcta
            if (crcCalculated == crcReceived && response[0] == slaveAddress && response[1] == 0x03) {
                uint8_t dataByteCount = response[2];
                
                // Verificar que la cantidad de bytes de datos coincida con lo esperado
                int expectedTotalBytes = 5 + dataByteCount; // 1 addr + 1 func + 1 count + dataBytes + 2 CRC
                
                if (dataByteCount == numRegisters * 2 && bytesRead == expectedTotalBytes) {
                    for (int i = 0; i < dataByteCount; i++) {
                        responseReceived[i] = response[3 + i];
                    }
                    success = true;
                }
            }
        }

        // Siempre devolver el semáforo al final
        xSemaphoreGive(sem);
    }

    return success;
}

