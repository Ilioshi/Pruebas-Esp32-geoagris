#include <Arduino.h>

// ============================================================
// GENERADOR RS485 / MODBUS RTU - CLIMA - DIAGNOSTICO
//
// Esclavo Modbus para probar el receptor/maestro.
// Mejora el archivo original agregando diagnostico de:
//   - bytes basura antes del Slave ID
//   - consultas parciales/cortadas
//   - CRC incorrecto
//   - Slave incorrecto
//   - funcion incorrecta
//   - cantidad invalida
//   - consulta no esperada
//   - tiempo sin consultas
//
// RS485:
//   TX    = GPIO17
//   RX    = GPIO16
//   DE/RE = GPIO4
//   UART1 = 9600 8N1
//
// Slave ID = 0xFF
// Funcion  = 0x03
// ============================================================

#define RS485_TX_PIN     17
#define RS485_RX_PIN     16
#define RS485_DE_RE_PIN  4

#define MODBUS_SLAVE_ID  0xFF
#define REQUEST_LEN      8
#define PARTIAL_TIMEOUT_MS 80

HardwareSerial RS485(1);

const uint16_t REG_TEMP       = 0x18B7;
const uint16_t REG_HUMIDITY   = 0x0BA3;
const uint16_t REG_PRESSURE   = 0x279D;
const uint16_t REG_WIND_SPEED = 0x007E;
const uint16_t REG_WIND_DIR   = 0x0D83;
const uint16_t REG_RAIN       = 0x0032;
const uint16_t REG_COMPASS    = 0x0045;

unsigned long totalValidQueries   = 0;
unsigned long totalResponses      = 0;
unsigned long totalBadCRC         = 0;
unsigned long totalPartial        = 0;
unsigned long totalNoiseBytes     = 0;
unsigned long totalWrongFunction  = 0;
unsigned long totalWrongQuery     = 0;
unsigned long lastValidQueryMs    = 0;
unsigned long lastAnyByteMs       = 0;
unsigned long lastStatusMs        = 0;
bool warnedNoValidQuery = false;

uint8_t request[REQUEST_LEN];
uint8_t requestIndex = 0;
unsigned long partialStartMs = 0;
unsigned long partialLastByteMs = 0;

uint16_t modbusCRC(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;

    for (int pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void printHexByte(uint8_t b)
{
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);
}

void printFrame(const char *prefix, const uint8_t *data, int length)
{
    Serial.print(prefix);
    for (int i = 0; i < length; i++)
    {
        printHexByte(data[i]);
        if (i + 1 < length) Serial.print(' ');
    }
    Serial.println();
}

bool getRegisterValue(uint16_t address, uint16_t &value)
{
    switch (address)
    {
        case 0x0009: value = REG_TEMP;       return true;
        case 0x000A: value = REG_HUMIDITY;   return true;
        case 0x000B: value = REG_PRESSURE;   return true;
        case 0x000C: value = REG_WIND_SPEED; return true;
        case 0x000D: value = REG_WIND_DIR;   return true;
        case 0x000E: value = REG_RAIN;       return true;
        case 0x0020: value = REG_COMPASS;    return true;
        default: value = 0; return false;
    }
}

void sendResponse(uint8_t slave, uint8_t function, const uint8_t *data, uint8_t dataLength)
{
    uint8_t response[32];
    int len = 0;

    response[len++] = slave;
    response[len++] = function;
    response[len++] = dataLength;

    for (int i = 0; i < dataLength; i++)
        response[len++] = data[i];

    uint16_t crc = modbusCRC(response, len);
    response[len++] = crc & 0xFF;
    response[len++] = (crc >> 8) & 0xFF;

    // Se imprime antes de transmitir para que ambos monitores puedan
    // compararse, pero la trama ya esta completamente construida.
    printFrame("TX RESPUESTA: ", response, len);

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485.write(response, len);
    RS485.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    totalResponses++;
}

void processRequest(const uint8_t req[REQUEST_LEN])
{
    Serial.println();
    printFrame("RX CONSULTA: ", req, REQUEST_LEN);

    uint16_t calcCRC = modbusCRC(req, 6);
    uint16_t recvCRC = ((uint16_t)req[7] << 8) | req[6];

    if (calcCRC != recvCRC)
    {
        totalBadCRC++;
        Serial.print("ERROR: CRC consulta. Calculado=0x");
        Serial.print(calcCRC, HEX);
        Serial.print(" recibido=0x");
        Serial.println(recvCRC, HEX);
        return;
    }

    if (req[0] != MODBUS_SLAVE_ID)
    {
        // En la practica no deberia llegar aqui porque el parser sincroniza
        // por 0xFF, pero se conserva la validacion.
        Serial.println("ERROR: Slave ID incorrecto");
        totalWrongQuery++;
        return;
    }

    if (req[1] != 0x03)
    {
        Serial.print("ERROR: funcion no soportada 0x");
        printHexByte(req[1]);
        Serial.println();
        totalWrongFunction++;
        return;
    }

    uint16_t firstRegister = ((uint16_t)req[2] << 8) | req[3];
    uint16_t numRegisters  = ((uint16_t)req[4] << 8) | req[5];

    if (numRegisters == 0 || numRegisters > 125)
    {
        Serial.println("ERROR: cantidad de registros invalida");
        totalWrongQuery++;
        return;
    }

    bool expected =
        (firstRegister == 0x0009 && numRegisters == 6) ||
        (firstRegister == 0x0020 && numRegisters == 1);

    if (!expected)
    {
        Serial.print("ERROR: consulta no esperada. Inicio=0x");
        Serial.print(firstRegister, HEX);
        Serial.print(" cantidad=");
        Serial.println(numRegisters);
        totalWrongQuery++;
        return;
    }

    uint8_t data[16];
    uint8_t dataLen = 0;

    for (uint16_t i = 0; i < numRegisters; i++)
    {
        uint16_t value = 0;
        uint16_t address = firstRegister + i;

        if (!getRegisterValue(address, value))
        {
            Serial.print("ERROR: registro sin valor 0x");
            Serial.println(address, HEX);
            totalWrongQuery++;
            return;
        }

        data[dataLen++] = (value >> 8) & 0xFF;
        data[dataLen++] = value & 0xFF;
    }

    totalValidQueries++;
    lastValidQueryMs = millis();
    warnedNoValidQuery = false;

    Serial.print("VALIDA: inicio=0x");
    Serial.print(firstRegister, HEX);
    Serial.print(" registros=");
    Serial.println(numRegisters);

    sendResponse(req[0], req[1], data, dataLen);
}

void resetPartial(const char *reason)
{
    if (requestIndex > 0)
    {
        totalPartial++;
        Serial.println();
        Serial.print("ERROR: consulta parcial descartada - ");
        Serial.println(reason);
        printFrame("RX PARCIAL: ", request, requestIndex);
    }

    requestIndex = 0;
    partialStartMs = 0;
    partialLastByteMs = 0;
}

void receiveBytes()
{
    while (RS485.available())
    {
        uint8_t b = (uint8_t)RS485.read();
        unsigned long now = millis();
        lastAnyByteMs = now;

        if (requestIndex == 0)
        {
            if (b != MODBUS_SLAVE_ID)
            {
                totalNoiseBytes++;
                Serial.print("[RUIDO/BYTE FUERA DE TRAMA] 0x");
                printHexByte(b);
                Serial.println();
                continue;
            }

            partialStartMs = now;
        }

        request[requestIndex++] = b;
        partialLastByteMs = now;

        if (requestIndex == REQUEST_LEN)
        {
            processRequest(request);
            requestIndex = 0;
            partialStartMs = 0;
            partialLastByteMs = 0;
        }
    }

    if (requestIndex > 0 &&
        (unsigned long)(millis() - partialLastByteMs) >= PARTIAL_TIMEOUT_MS)
    {
        resetPartial("timeout entre bytes");
    }
}

void showStatus()
{
    unsigned long now = millis();
    if ((unsigned long)(now - lastStatusMs) < 2000) return;
    lastStatusMs = now;

    Serial.print("[STATUS] consultas_validas="); Serial.print(totalValidQueries);
    Serial.print(" respuestas="); Serial.print(totalResponses);
    Serial.print(" crc_bad="); Serial.print(totalBadCRC);
    Serial.print(" parciales="); Serial.print(totalPartial);
    Serial.print(" ruido_bytes="); Serial.print(totalNoiseBytes);
    Serial.print(" funcion_bad="); Serial.print(totalWrongFunction);
    Serial.print(" consulta_bad="); Serial.println(totalWrongQuery);

    if (totalValidQueries == 0 && now >= 10000 && !warnedNoValidQuery)
    {
        Serial.println("ALERTA: 10 s sin ninguna consulta Modbus valida.");
        Serial.println("Si el maestro esta transmitiendo, revisar ida RS485, A/B, GND, baud y DE/RE.");
        warnedNoValidQuery = true;
    }
    else if (totalValidQueries > 0 &&
             (unsigned long)(now - lastValidQueryMs) >= 10000 &&
             !warnedNoValidQuery)
    {
        Serial.println("ALERTA: la comunicacion se detuvo; hace 10 s que no entra una consulta valida.");
        warnedNoValidQuery = true;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("============================================================");
    Serial.println(" GENERADOR RS485 CLIMA - ESCLAVO MODBUS DE DIAGNOSTICO");
    Serial.println("============================================================");
    Serial.println("TX=GPIO17 RX=GPIO16 DE/RE=GPIO4 | UART1 9600 8N1");
    Serial.println("Slave=0xFF | Funcion=0x03");
    Serial.println("Esperando 0x0009 x6 y 0x0020 x1");

    RS485.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    lastValidQueryMs = millis();
    lastAnyByteMs = millis();
}

void loop()
{
    receiveBytes();
    showStatus();
}
