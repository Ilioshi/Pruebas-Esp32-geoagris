#include <Arduino.h>

// ============================================================
// RS485 / MODBUS RTU - MAESTRO RECEPTOR DE DIAGNOSTICO
//
// Diseñado para probar el generador de clima Geoagris.
// Detecta y diferencia:
//   - sin respuesta
//   - eco de la consulta
//   - bytes basura / trafico inesperado
//   - trama parcial / cortada
//   - CRC incorrecto
//   - Slave ID incorrecto
//   - funcion incorrecta
//   - excepcion Modbus
//   - byte-count / longitud incorrectos
//   - datos validos pero distintos a los esperados
//   - bytes extra antes/despues de una respuesta valida
//   - multiples respuestas a una sola consulta
//
// RS485:
//   TX    = GPIO17
//   RX    = GPIO16
//   DE/RE = GPIO4
//   UART1 = 9600 8N1
//
// Generador esperado:
//   Slave = 0xFF
//   Funcion = 0x03
// ============================================================

#define RS485_TX_PIN     17
#define RS485_RX_PIN     16
#define RS485_DE_RE_PIN  4

#define MODBUS_SLAVE_ID  0xFF
#define MODBUS_FUNCTION  0x03

#define RESPONSE_TIMEOUT_MS      700
#define INTERBYTE_TIMEOUT_MS      80
#define BETWEEN_TESTS_MS         250
#define CYCLE_INTERVAL_MS       2000
#define PRE_TX_GUARD_US           50
#define POST_TX_GUARD_US          50

#define RX_BUFFER_SIZE           128

HardwareSerial RS485(1);

// Valores exactos que debe devolver el generador suministrado.
const uint16_t EXPECTED_WEATHER[6] = {
    0x18B7, // temperatura
    0x0BA3, // humedad
    0x279D, // presion
    0x007E, // velocidad viento
    0x0D83, // direccion viento
    0x0032  // lluvia
};

const uint16_t EXPECTED_COMPASS = 0x0045;

// ============================================================
// ESTADISTICAS
// ============================================================

unsigned long statQueries            = 0;
unsigned long statPerfect            = 0;
unsigned long statNoData             = 0;
unsigned long statEchoOnly           = 0;
unsigned long statPartial            = 0;
unsigned long statBadCRC             = 0;
unsigned long statWrongSlave         = 0;
unsigned long statWrongFunction      = 0;
unsigned long statExceptions         = 0;
unsigned long statWrongLength        = 0;
unsigned long statWrongData          = 0;
unsigned long statNoise              = 0;
unsigned long statExtraBytes         = 0;
unsigned long statMultipleResponses  = 0;
unsigned long statEchoSeen           = 0;
unsigned long statRxOverflow         = 0;
unsigned long statTotalRxBytes       = 0;
unsigned long statCycles             = 0;
unsigned long statCyclesOK           = 0;

// ============================================================
// CRC16 MODBUS
// ============================================================

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

// ============================================================
// UTILIDADES
// ============================================================

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

bool sameBytes(const uint8_t *a, const uint8_t *b, int len)
{
    for (int i = 0; i < len; i++)
        if (a[i] != b[i]) return false;

    return true;
}

uint16_t readU16BE(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

bool crcIsValid(const uint8_t *frame, int len)
{
    if (len < 4) return false;

    uint16_t calc = modbusCRC(frame, len - 2);
    uint16_t recv = ((uint16_t)frame[len - 1] << 8) | frame[len - 2];
    return calc == recv;
}

// ============================================================
// DRENAR TRAFICO ANTES DE UNA CONSULTA
// ============================================================

int drainUnexpectedBytes()
{
    uint8_t temp[RX_BUFFER_SIZE];
    int n = 0;

    while (RS485.available())
    {
        uint8_t b = (uint8_t)RS485.read();
        if (n < RX_BUFFER_SIZE) temp[n++] = b;
        statTotalRxBytes++;
    }

    if (n > 0)
    {
        statNoise++;
        Serial.println("AVISO: habia trafico RX antes de enviar la consulta.");
        printFrame("RX PREVIO: ", temp, n);
    }

    return n;
}

// ============================================================
// CONSTRUIR Y ENVIAR CONSULTA
// ============================================================

void buildQuery(uint16_t firstRegister, uint16_t numRegisters, uint8_t query[8])
{
    query[0] = MODBUS_SLAVE_ID;
    query[1] = MODBUS_FUNCTION;
    query[2] = (firstRegister >> 8) & 0xFF;
    query[3] = firstRegister & 0xFF;
    query[4] = (numRegisters >> 8) & 0xFF;
    query[5] = numRegisters & 0xFF;

    uint16_t crc = modbusCRC(query, 6);
    query[6] = crc & 0xFF;
    query[7] = (crc >> 8) & 0xFF;
}

void sendQuery(const uint8_t query[8])
{
    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(PRE_TX_GUARD_US);

    RS485.write(query, 8);
    RS485.flush();

    delayMicroseconds(POST_TX_GUARD_US);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    statQueries++;
}

// ============================================================
// CAPTURAR TODO LO QUE APAREZCA EN RX
//
// No intenta interpretar mientras recibe. Primero conserva la
// evidencia cruda y luego la analiza. Asi no se pierde ruido,
// eco ni una trama corrida.
// ============================================================

int captureRX(uint8_t *raw, int maxLen, unsigned long &firstByteDelayUs, bool &overflow)
{
    int len = 0;
    bool gotAny = false;

    unsigned long startMs = millis();
    unsigned long startUs = micros();
    unsigned long lastByteMs = startMs;
    firstByteDelayUs = 0;
    overflow = false;

    while ((unsigned long)(millis() - startMs) < RESPONSE_TIMEOUT_MS)
    {
        while (RS485.available())
        {
            uint8_t b = (uint8_t)RS485.read();

            if (!gotAny)
            {
                firstByteDelayUs = micros() - startUs;
                gotAny = true;
            }

            lastByteMs = millis();
            statTotalRxBytes++;

            if (len < maxLen)
            {
                raw[len++] = b;
            }
            else
            {
                // Seguimos drenando para no contaminar la prueba siguiente.
                if (!overflow)
                    Serial.println("ERROR: RX excedio el buffer de diagnostico.");
                overflow = true;
            }
        }

        if (gotAny && (unsigned long)(millis() - lastByteMs) >= INTERBYTE_TIMEOUT_MS)
            break;

        delay(1);
    }

    return len;
}

// ============================================================
// QUITAR ECOS EXACTOS DE LA CONSULTA
// ============================================================

int removeQueryEchoes(
    const uint8_t *raw,
    int rawLen,
    const uint8_t query[8],
    uint8_t *filtered,
    int filteredMax,
    int &echoCount
)
{
    int out = 0;
    echoCount = 0;

    for (int i = 0; i < rawLen;)
    {
        if (i + 8 <= rawLen && sameBytes(&raw[i], query, 8))
        {
            echoCount++;
            i += 8;
            continue;
        }

        if (out < filteredMax)
            filtered[out++] = raw[i];

        i++;
    }

    return out;
}

// ============================================================
// DESCRIBIR EXCEPCION MODBUS
// ============================================================

const char *exceptionText(uint8_t code)
{
    switch (code)
    {
        case 0x01: return "Illegal Function";
        case 0x02: return "Illegal Data Address";
        case 0x03: return "Illegal Data Value";
        case 0x04: return "Slave Device Failure";
        case 0x05: return "Acknowledge";
        case 0x06: return "Slave Device Busy";
        case 0x08: return "Memory Parity Error";
        case 0x0A: return "Gateway Path Unavailable";
        case 0x0B: return "Gateway Target Failed to Respond";
        default:   return "Codigo de excepcion desconocido";
    }
}

// ============================================================
// BUSCAR TRAMAS MODBUS CRC-VALIDAS EN UN BLOQUE DE BYTES
//
// Devuelve cuantas tramas plausibles y CRC-validas encontro.
// Guarda la posicion/longitud de la primera.
// ============================================================

int findValidFrames(const uint8_t *data, int len, int &firstPos, int &firstLen)
{
    int count = 0;
    firstPos = -1;
    firstLen = 0;

    for (int i = 0; i <= len - 5; i++)
    {
        uint8_t function = data[i + 1];
        int frameLen = 0;

        if (function & 0x80)
        {
            frameLen = 5;
        }
        else if (function >= 0x01 && function <= 0x04)
        {
            uint8_t byteCount = data[i + 2];
            frameLen = 3 + byteCount + 2;

            if (frameLen < 5 || frameLen > RX_BUFFER_SIZE)
                continue;
        }
        else
        {
            continue;
        }

        if (i + frameLen > len)
            continue;

        if (crcIsValid(&data[i], frameLen))
        {
            if (count == 0)
            {
                firstPos = i;
                firstLen = frameLen;
            }
            count++;
        }
    }

    return count;
}

// ============================================================
// MOSTRAR VALORES DECODIFICADOS
// ============================================================

void showWeatherValues(const uint8_t *frame)
{
    uint16_t rawTemp      = readU16BE(&frame[3]);
    uint16_t rawHum       = readU16BE(&frame[5]);
    uint16_t rawPressure  = readU16BE(&frame[7]);
    uint16_t rawWind      = readU16BE(&frame[9]);
    uint16_t rawWindDir   = readU16BE(&frame[11]);
    uint16_t rawRain      = readU16BE(&frame[13]);

    Serial.println("DATOS DECODIFICADOS:");
    Serial.print("  Temperatura      = ");
    Serial.print((rawTemp / 100.0f) - 40.0f, 2);
    Serial.println(" C");

    Serial.print("  Humedad          = ");
    Serial.print(rawHum / 100.0f, 2);
    Serial.println(" %");

    Serial.print("  Presion          = ");
    Serial.print(rawPressure / 10.0f, 1);
    Serial.println(" hPa");

    Serial.print("  Viento           = ");
    Serial.print(rawWind / 100.0f, 2);
    Serial.println(" m/s");

    Serial.print("  Direccion viento = ");
    Serial.print(rawWindDir / 10.0f, 1);
    Serial.println(" grados");

    Serial.print("  Precipitacion    = ");
    Serial.print(rawRain / 10.0f, 1);
    Serial.println(" mm");
}

void showCompassValue(const uint8_t *frame)
{
    uint16_t compass = readU16BE(&frame[3]);
    Serial.print("DATOS DECODIFICADOS: Compass = ");
    Serial.print((float)compass, 1);
    Serial.println(" grados");
}

// ============================================================
// COMPARAR PAYLOAD EXACTO CON EL GENERADOR
// ============================================================

bool payloadMatchesExpected(
    const uint8_t *frame,
    uint16_t firstRegister,
    uint16_t numRegisters
)
{
    if (firstRegister == 0x0009 && numRegisters == 6)
    {
        for (int i = 0; i < 6; i++)
        {
            uint16_t got = readU16BE(&frame[3 + i * 2]);
            if (got != EXPECTED_WEATHER[i])
            {
                Serial.print("ERROR: registro 0x");
                printHexByte(0x00);
                printHexByte((uint8_t)(0x09 + i));
                Serial.print(" esperado=0x");
                if (EXPECTED_WEATHER[i] < 0x1000) Serial.print('0');
                if (EXPECTED_WEATHER[i] < 0x0100) Serial.print('0');
                if (EXPECTED_WEATHER[i] < 0x0010) Serial.print('0');
                Serial.print(EXPECTED_WEATHER[i], HEX);
                Serial.print(" recibido=0x");
                if (got < 0x1000) Serial.print('0');
                if (got < 0x0100) Serial.print('0');
                if (got < 0x0010) Serial.print('0');
                Serial.println(got, HEX);
                return false;
            }
        }
        return true;
    }

    if (firstRegister == 0x0020 && numRegisters == 1)
    {
        uint16_t got = readU16BE(&frame[3]);
        if (got != EXPECTED_COMPASS)
        {
            Serial.print("ERROR: Compass esperado=0x0045 recibido=0x");
            if (got < 0x1000) Serial.print('0');
            if (got < 0x0100) Serial.print('0');
            if (got < 0x0010) Serial.print('0');
            Serial.println(got, HEX);
            return false;
        }
        return true;
    }

    return false;
}

// ============================================================
// ANALIZAR UNA TRANSACCION
//
// Devuelve true SOLO si la respuesta es exactamente la esperada
// y no hay bytes extra/noise en esa captura.
// ============================================================

bool runTransaction(uint16_t firstRegister, uint16_t numRegisters, const char *name)
{
    uint8_t query[8];
    uint8_t raw[RX_BUFFER_SIZE];
    uint8_t filtered[RX_BUFFER_SIZE];

    buildQuery(firstRegister, numRegisters, query);

    Serial.println();
    Serial.println("============================================================");
    Serial.print("PRUEBA: ");
    Serial.println(name);

    int preNoise = drainUnexpectedBytes();

    printFrame("TX: ", query, 8);
    sendQuery(query);

    unsigned long firstByteDelayUs = 0;
    bool rxOverflow = false;
    int rawLen = captureRX(raw, RX_BUFFER_SIZE, firstByteDelayUs, rxOverflow);

    if (rawLen == 0)
    {
        statNoData++;
        Serial.println("RESULTADO: FALLA - SIN RESPUESTA");
        Serial.println("No aparecio ningun byte en RX despues de la consulta.");
        Serial.println("Con ambos monitores serie se puede separar la falla:");
        Serial.println("  - si el generador NO ve la consulta: falla en ida/TX/cableado/baud/DE-RE");
        Serial.println("  - si el generador SI la ve y responde: falla en vuelta/RX/cableado/DE-RE");
        return false;
    }

    if (rxOverflow)
    {
        statRxOverflow++;
        Serial.println("RESULTADO: FALLA - DEMASIADOS BYTES RX / BUFFER SATURADO");
        printFrame("RX PRIMEROS BYTES: ", raw, rawLen);
        return false;
    }

    Serial.print("Primer byte RX despues de aprox. ");
    Serial.print(firstByteDelayUs);
    Serial.println(" us");
    printFrame("RX CRUDO: ", raw, rawLen);

    int echoCount = 0;
    int filteredLen = removeQueryEchoes(raw, rawLen, query, filtered, RX_BUFFER_SIZE, echoCount);

    if (echoCount > 0)
    {
        statEchoSeen += echoCount;
        Serial.print("INFO: eco exacto de la consulta detectado x");
        Serial.println(echoCount);
    }

    if (filteredLen == 0)
    {
        statEchoOnly++;
        Serial.println("RESULTADO: FALLA - SOLO SE RECIBIO EL ECO DE TX");
        Serial.println("No llego una respuesta Modbus del esclavo.");
        return false;
    }

    if (echoCount > 0)
        printFrame("RX SIN ECO: ", filtered, filteredLen);

    int firstPos = -1;
    int firstLen = 0;
    int validFrameCount = findValidFrames(filtered, filteredLen, firstPos, firstLen);

    // --------------------------------------------------------
    // Si no existe ninguna trama CRC-valida, intentar explicar
    // si parece una respuesta cortada o una respuesta con CRC malo.
    // --------------------------------------------------------
    if (validFrameCount == 0)
    {
        bool sawExpectedHeader = false;

        for (int i = 0; i < filteredLen; i++)
        {
            if (filtered[i] != MODBUS_SLAVE_ID)
                continue;

            sawExpectedHeader = true;

            if (i + 2 >= filteredLen)
            {
                statPartial++;
                Serial.println("RESULTADO: FALLA - TRAMA PARCIAL (cabecera incompleta)");
                return false;
            }

            uint8_t function = filtered[i + 1];

            if (function == MODBUS_FUNCTION)
            {
                uint8_t byteCount = filtered[i + 2];
                int declaredLen = 3 + byteCount + 2;

                if (i + declaredLen > filteredLen)
                {
                    statPartial++;
                    Serial.print("RESULTADO: FALLA - TRAMA CORTADA. Declaraba ");
                    Serial.print(declaredLen);
                    Serial.print(" bytes y solo hay ");
                    Serial.println(filteredLen - i);
                    return false;
                }

                statBadCRC++;
                uint16_t calc = modbusCRC(&filtered[i], declaredLen - 2);
                uint16_t recv = ((uint16_t)filtered[i + declaredLen - 1] << 8) |
                                filtered[i + declaredLen - 2];
                Serial.print("RESULTADO: FALLA - CRC INCORRECTO. Calculado=0x");
                Serial.print(calc, HEX);
                Serial.print(" recibido=0x");
                Serial.println(recv, HEX);
                return false;
            }

            if (function & 0x80)
            {
                if (i + 5 > filteredLen)
                {
                    statPartial++;
                    Serial.println("RESULTADO: FALLA - EXCEPCION MODBUS CORTADA");
                    return false;
                }

                statBadCRC++;
                Serial.println("RESULTADO: FALLA - posible excepcion con CRC incorrecto");
                return false;
            }
        }

        if (sawExpectedHeader)
        {
            statWrongFunction++;
            Serial.println("RESULTADO: FALLA - SLAVE CORRECTO PERO RESPUESTA NO INTERPRETABLE");
        }
        else
        {
            statNoise++;
            Serial.println("RESULTADO: FALLA - HAY BYTES, PERO NO FORMAN UNA RESPUESTA MODBUS VALIDA");
            Serial.println("Posible ruido, baud/formato incorrecto, A/B/cableado o datos corruptos.");
        }

        return false;
    }

    if (validFrameCount > 1)
    {
        statMultipleResponses++;
        Serial.print("ERROR: se detectaron ");
        Serial.print(validFrameCount);
        Serial.println(" tramas CRC-validas para una sola consulta.");
    }

    const uint8_t *frame = &filtered[firstPos];

    Serial.print("TRAMA MODBUS CRC-VALIDA encontrada en offset ");
    Serial.println(firstPos);
    printFrame("FRAME: ", frame, firstLen);

    // --------------------------------------------------------
    // Validaciones semanticas
    // --------------------------------------------------------
    if (frame[0] != MODBUS_SLAVE_ID)
    {
        statWrongSlave++;
        Serial.print("RESULTADO: FALLA - SLAVE ID INCORRECTO. Esperado=0xFF recibido=0x");
        printHexByte(frame[0]);
        Serial.println();
        return false;
    }

    if (frame[1] & 0x80)
    {
        statExceptions++;
        Serial.print("RESULTADO: FALLA - EXCEPCION MODBUS 0x");
        printHexByte(frame[2]);
        Serial.print(" (");
        Serial.print(exceptionText(frame[2]));
        Serial.println(")");
        return false;
    }

    if (frame[1] != MODBUS_FUNCTION)
    {
        statWrongFunction++;
        Serial.print("RESULTADO: FALLA - FUNCION INCORRECTA. Esperada=0x03 recibida=0x");
        printHexByte(frame[1]);
        Serial.println();
        return false;
    }

    uint8_t expectedByteCount = (uint8_t)(numRegisters * 2);
    int expectedFrameLen = 3 + expectedByteCount + 2;

    if (frame[2] != expectedByteCount || firstLen != expectedFrameLen)
    {
        statWrongLength++;
        Serial.print("RESULTADO: FALLA - LONGITUD/BYTE-COUNT INCORRECTO. Esperado data=");
        Serial.print(expectedByteCount);
        Serial.print(" frame=");
        Serial.print(expectedFrameLen);
        Serial.print(" | recibido data=");
        Serial.print(frame[2]);
        Serial.print(" frame=");
        Serial.println(firstLen);
        return false;
    }

    bool dataOK = payloadMatchesExpected(frame, firstRegister, numRegisters);

    if (!dataOK)
    {
        statWrongData++;
        Serial.println("RESULTADO: FALLA - MODBUS ES VALIDO PERO LOS DATOS NO SON LOS ESPERADOS");
        return false;
    }

    if (firstRegister == 0x0009)
        showWeatherValues(frame);
    else if (firstRegister == 0x0020)
        showCompassValue(frame);

    // Bytes fuera de la trama valida = anomalia, aunque la respuesta sea buena.
    int extra = filteredLen - firstLen;
    if (extra > 0 || firstPos != 0 || validFrameCount > 1 || preNoise > 0)
    {
        statExtraBytes++;
        Serial.println("RESULTADO: ADVERTENCIA - RESPUESTA CORRECTA, PERO HAY TRAFICO EXTRA/ANOMALO");
        if (firstPos > 0)
        {
            Serial.print("Bytes antes de la trama: ");
            Serial.println(firstPos);
        }
        if (filteredLen - (firstPos + firstLen) > 0)
        {
            Serial.print("Bytes despues de la trama: ");
            Serial.println(filteredLen - (firstPos + firstLen));
        }
        return false;
    }

    statPerfect++;
    Serial.println("RESULTADO: OK PERFECTO - trama, CRC, direccion, funcion, longitud y datos correctos");
    return true;
}

// ============================================================
// RESUMEN GENERAL
// ============================================================

void showSummary()
{
    Serial.println();
    Serial.println("================ RESUMEN GENERAL ================");
    Serial.print("Ciclos completos:        "); Serial.println(statCycles);
    Serial.print("Ciclos OK:               "); Serial.println(statCyclesOK);
    Serial.print("Consultas enviadas:      "); Serial.println(statQueries);
    Serial.print("Respuestas perfectas:    "); Serial.println(statPerfect);
    Serial.print("Sin datos:                "); Serial.println(statNoData);
    Serial.print("Solo eco:                 "); Serial.println(statEchoOnly);
    Serial.print("Tramas parciales:         "); Serial.println(statPartial);
    Serial.print("CRC incorrecto:           "); Serial.println(statBadCRC);
    Serial.print("Slave incorrecto:         "); Serial.println(statWrongSlave);
    Serial.print("Funcion incorrecta:       "); Serial.println(statWrongFunction);
    Serial.print("Excepciones Modbus:       "); Serial.println(statExceptions);
    Serial.print("Longitud incorrecta:      "); Serial.println(statWrongLength);
    Serial.print("Datos incorrectos:        "); Serial.println(statWrongData);
    Serial.print("Ruido/trafico previo:     "); Serial.println(statNoise);
    Serial.print("Respuestas con extras:    "); Serial.println(statExtraBytes);
    Serial.print("Respuestas multiples:     "); Serial.println(statMultipleResponses);
    Serial.print("Ecos detectados:          "); Serial.println(statEchoSeen);
    Serial.print("Overflow RX:              "); Serial.println(statRxOverflow);
    Serial.print("Bytes RX totales:         "); Serial.println(statTotalRxBytes);
    Serial.println("=================================================");
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("============================================================");
    Serial.println(" RS485 MODBUS - RECEPTOR / MAESTRO DE DIAGNOSTICO COMPLETO");
    Serial.println("============================================================");
    Serial.println("TX=GPIO17 RX=GPIO16 DE/RE=GPIO4 | UART1 9600 8N1");
    Serial.println("Slave esperado=0xFF | Funcion esperada=0x03");

    RS485.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    while (RS485.available()) RS485.read();

    Serial.println("Listo. Se probaran WEATHER y COMPASS en cada ciclo.");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    statCycles++;

    Serial.println();
    Serial.println();
    Serial.print("################ CICLO #");
    Serial.print(statCycles);
    Serial.println(" ################");

    bool weatherOK = runTransaction(0x0009, 6, "WEATHER 0x0009..0x000E");

    delay(BETWEEN_TESTS_MS);

    bool compassOK = runTransaction(0x0020, 1, "COMPASS 0x0020");

    if (weatherOK && compassOK)
    {
        statCyclesOK++;
        Serial.println();
        Serial.println("CICLO COMPLETO: OK");
    }
    else
    {
        Serial.println();
        Serial.println("CICLO COMPLETO: FALLA O ANOMALIA DETECTADA");
    }

    showSummary();
    delay(CYCLE_INTERVAL_MS);
}
