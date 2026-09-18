/**
 * @file VG55R.cpp
 * @brief Implementación del driver para sensor VG55R
 */

#include "CAN/CAN_protocols/VG55R.h"

// ============================================================================
// CONSTRUCTOR E INICIALIZACIÓN
// ============================================================================

VG55R_Driver::VG55R_Driver(MCP2515* mcp, uint8_t nodeID)
    : mcp(mcp), nodeID(nodeID), detected(false), lastRxTime(0), framePeriod(0),
      autoDetectStartTime(0), lastError(VG55R_OK), bcdErrorCount(0), txErrorCount(0)
{
    // Calcular IDs basados en el nodeID
    dataID = 0x580 + nodeID;
    cmdID = 0x600 + nodeID;

    // Inicializar estructuras de datos
    lastAngles = {0, 0, 0, 0, false};
    lastGyro = {0, 0, 0, 0, false};
    lastAccel = {0, 0, 0, 0, false};
    lastQuaternion = {0, 0, 0, 0, 0, false};
}

VG55R_Error VG55R_Driver::begin()
{
    // El bus CAN ya debe estar configurado externamente
    // Solo verificamos que el puntero MCP2515 es válido
    if (!mcp) {
        setError(VG55R_ERR_MCP_NULL);
        return VG55R_ERR_MCP_NULL;
    }

    lastError = VG55R_OK;
    return VG55R_OK;
}

void VG55R_Driver::startAutoDetect()
{
    autoDetectStartTime = millis();
    detected = false;
    lastError = VG55R_OK;
}

VG55R_Error VG55R_Driver::checkAutoDetect(uint32_t timeoutMs)
{
    // Si ya está detectado, retornar OK
    if (detected) {
        return VG55R_OK;
    }

    // Si no se ha iniciado la detección
    if (autoDetectStartTime == 0) {
        setError(VG55R_ERR_NOT_DETECTED);
        return VG55R_ERR_NOT_DETECTED;
    }

    // Verificar timeout
    uint32_t elapsed = millis() - autoDetectStartTime;
    if (elapsed >= timeoutMs) {
        setError(VG55R_ERR_TIMEOUT);
        autoDetectStartTime = 0;  // Reset
        return VG55R_ERR_TIMEOUT;
    }

    // Aún buscando
    return VG55R_ERR_NOT_DETECTED;
}

// ============================================================================
// PROCESAMIENTO DE FRAMES
// ============================================================================

bool VG55R_Driver::processFrame(const struct can_frame& frame)
{
    // Solo frames estándar de 11 bits
    if (frame.can_id & CAN_EFF_FLAG) {
        return false;
    }

    uint16_t id = frame.can_id & CAN_SFF_MASK;

    // Auto-detección: Si recibimos un frame 0x580-0x5FF con 8 bytes
    if (id >= 0x580 && id <= 0x5FF && frame.can_dlc == 8) {
        // Si no estamos detectados, auto-configurar el nodeID
        if (!detected || id == dataID) {
            if (!detected) {
                // Primera detección: configurar IDs
                nodeID = (uint8_t)(id - 0x580);
                dataID = id;
                cmdID = 0x600 + nodeID;
                detected = true;
            }
            
            // Actualizar timestamp de recepción
            uint32_t now = millis();
            if (lastRxTime > 0) {
                framePeriod = now - lastRxTime;
            }
            lastRxTime = now;

            // El tipo de dato depende del modo configurado
            // Por defecto, asumimos modo ángulos (0x00)
            processAngleFrame(frame.data);

            return true;
        }
    }

    return false;
}

void VG55R_Driver::processAngleFrame(const uint8_t* data)
{
    // Formato: D0-D2 = PITCH, D3-D5 = ROLL, D6-D7 = YAW
    float p = decodePitchRoll(&data[0]);
    float r = decodePitchRoll(&data[3]);
    float y = decodeYaw(&data[6]);

    // Validar que los valores estén en rangos razonables
    bool valid = !isnan(p) && !isnan(r) && !isnan(y) &&
                 (p >= -180.0f && p <= 180.0f) &&
                 (r >= -180.0f && r <= 180.0f) &&
                 (y >= 0.0f && y <= 360.0f);

    if (valid) {
        lastAngles.pitch = p;
        lastAngles.roll = r;
        lastAngles.yaw = y;
        lastAngles.timestamp = millis();
        lastAngles.valid = true;
    } else {
        // Datos BCD inválidos - incrementar contador
        bcdErrorCount++;
        setError(VG55R_ERR_INVALID_BCD);
    }
}

void VG55R_Driver::processGyroFrame(const uint8_t* data)
{
    // Formato: D1-D2 = GyroX, D3-D4 = GyroY, D5-D6 = GyroZ
    // D0 = 0x50, D1 = 0x00 (header)
    float gx = decodeGyro(&data[2]);
    float gy = decodeGyro(&data[4]);
    float gz = decodeGyro(&data[6]);

    if (!isnan(gx) && !isnan(gy) && !isnan(gz)) {
        lastGyro.gyroX = gx;
        lastGyro.gyroY = gy;
        lastGyro.gyroZ = gz;
        lastGyro.timestamp = millis();
        lastGyro.valid = true;
    } else {
        lastGyro.valid = false;
    }
}

void VG55R_Driver::processAccelFrame(const uint8_t* data)
{
    // Formato: D1-D2 = AccX, D3-D4 = AccY, D5-D6 = AccZ
    // D0 = 0x54, D1 = 0x00 (header)
    float ax = decodeAccel(&data[2]);
    float ay = decodeAccel(&data[4]);
    float az = decodeAccel(&data[6]);

    if (!isnan(ax) && !isnan(ay) && !isnan(az)) {
        lastAccel.accX = ax;
        lastAccel.accY = ay;
        lastAccel.accZ = az;
        lastAccel.timestamp = millis();
        lastAccel.valid = true;
    } else {
        lastAccel.valid = false;
    }
}

void VG55R_Driver::processQuaternionFrame(const uint8_t* data, bool firstHalf)
{
    // El cuaternión viene en 2 frames de 8 bytes
    // Primera mitad: Q0, Q1
    // Segunda mitad: Q2, Q3
    
    if (firstHalf) {
        float q0 = decodeQuaternion(&data[0]);
        float q1 = decodeQuaternion(&data[4]);
        
        if (!isnan(q0) && !isnan(q1)) {
            lastQuaternion.q0 = q0;
            lastQuaternion.q1 = q1;
        }
    } else {
        float q2 = decodeQuaternion(&data[0]);
        float q3 = decodeQuaternion(&data[4]);
        
        if (!isnan(q2) && !isnan(q3)) {
            lastQuaternion.q2 = q2;
            lastQuaternion.q3 = q3;
            lastQuaternion.timestamp = millis();
            lastQuaternion.valid = true;
        }
    }
}

// ============================================================================
// DECODIFICACIÓN BCD
// ============================================================================

float VG55R_Driver::decodePitchRoll(const uint8_t* data)
{
    // Formato: SZZZ.YY (3 bytes)
    // S = signo (0=+, 1=-)
    // ZZZ = entero (3 dígitos)
    // YY = decimal (2 dígitos)
    
    int s = nibbleHi(data[0]);
    int c = nibbleLo(data[0]);  // centenas
    int d = nibbleHi(data[1]);  // decenas
    int u = nibbleLo(data[1]);  // unidades
    int t1 = nibbleHi(data[2]); // décima
    int t2 = nibbleLo(data[2]); // centésima

    // Validar BCD
    if (c > 9 || d > 9 || u > 9 || t1 > 9 || t2 > 9) {
        return NAN;
    }

    float value = (c * 100 + d * 10 + u) + (t1 * 10 + t2) / 100.0f;
    return (s == 1) ? -value : value;
}

float VG55R_Driver::decodeYaw(const uint8_t* data)
{
    // Formato: XX.YY (2 bytes)
    // XX = entero (2 dígitos)
    // YY = decimal (2 dígitos)
    
    int d = nibbleHi(data[0]);  // decenas
    int u = nibbleLo(data[0]);  // unidades
    int t1 = nibbleHi(data[1]); // décima
    int t2 = nibbleLo(data[1]); // centésima

    // Validar BCD
    if (d > 9 || u > 9 || t1 > 9 || t2 > 9) {
        return NAN;
    }

    return (d * 10 + u) + (t1 * 10 + t2) / 100.0f;
}

float VG55R_Driver::decodeGyro(const uint8_t* data)
{
    // Formato: XXXX (2 bytes BCD)
    // Valor = (XXXX - 5000) / 10
    
    int d3 = nibbleHi(data[0]);
    int d2 = nibbleLo(data[0]);
    int d1 = nibbleHi(data[1]);
    int d0 = nibbleLo(data[1]);

    // Validar BCD
    if (d3 > 9 || d2 > 9 || d1 > 9 || d0 > 9) {
        return NAN;
    }

    int raw = d3 * 1000 + d2 * 100 + d1 * 10 + d0;
    return (raw - 5000) / 10.0f;
}

float VG55R_Driver::decodeAccel(const uint8_t* data)
{
    // Formato: XXXX (2 bytes BCD)
    // Valor = (XXXX - 5000) / 2500
    
    int d3 = nibbleHi(data[0]);
    int d2 = nibbleLo(data[0]);
    int d1 = nibbleHi(data[1]);
    int d0 = nibbleLo(data[1]);

    // Validar BCD
    if (d3 > 9 || d2 > 9 || d1 > 9 || d0 > 9) {
        return NAN;
    }

    int raw = d3 * 1000 + d2 * 100 + d1 * 10 + d0;
    return (raw - 5000) / 2500.0f;
}

float VG55R_Driver::decodeQuaternion(const uint8_t* data)
{
    // Formato: QSXYYYYY (4 bytes)
    // Q = número de cuaternión (0-3)
    // S = signo (0=+, 1=-)
    // X = entero (1 dígito)
    // YYYYY = decimal (5 dígitos)
    
    // Byte 0: QS (Q en nibble alto, S en nibble bajo pero solo bit 0)
    int s = nibbleLo(data[0]) & 0x01;
    
    // Byte 1: XY (X en nibble alto, Y1 en nibble bajo)
    int x = nibbleHi(data[1]);
    int y1 = nibbleLo(data[1]);
    
    // Bytes 2-3: YYYY (4 dígitos decimales más)
    int y2 = nibbleHi(data[2]);
    int y3 = nibbleLo(data[2]);
    int y4 = nibbleHi(data[3]);
    int y5 = nibbleLo(data[3]);

    // Validar BCD
    if (x > 9 || y1 > 9 || y2 > 9 || y3 > 9 || y4 > 9 || y5 > 9) {
        return NAN;
    }

    float value = x + (y1 * 10000 + y2 * 1000 + y3 * 100 + y4 * 10 + y5) / 100000.0f;
    return (s == 1) ? -value : value;
}

// ============================================================================
// COMANDOS DE SOLICITUD (NO BLOQUEANTES)
// ============================================================================

VG55R_Error VG55R_Driver::requestPitch()
{
    // Comando: 40 01 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestRoll()
{
    // Comando: 40 02 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestYaw()
{
    // Comando: 40 03 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestAngles()
{
    // Comando: 40 04 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestGyroscope()
{
    // Comando: 40 50 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x50, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestAccelerometer()
{
    // Comando: 40 54 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x54, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::requestQuaternion()
{
    // Comando: 40 57 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x57, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

// ============================================================================
// COMANDOS DE CONFIGURACIÓN
// ============================================================================

VG55R_Error VG55R_Driver::setZeroType(VG55R_ZeroType zeroType)
{
    // Comando: 40 05 10 00 ST 00 00 00
    uint8_t cmd[8] = {0x40, 0x05, 0x10, 0x00, (uint8_t)zeroType, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::saveConfiguration()
{
    // Comando: 40 0A 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x0A, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::setAutoOutputFrequency(VG55R_OutputFreq freq)
{
    // Comando: 40 0C 10 00 ST 00 00 00
    uint8_t cmd[8] = {0x40, 0x0C, 0x10, 0x00, (uint8_t)freq, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::setAutoDataMode(VG55R_AutoDataMode mode)
{
    // Comando: 40 56 10 00 ST 00 00 00
    uint8_t cmd[8] = {0x40, 0x56, 0x10, 0x00, (uint8_t)mode, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::clearGyroBias(bool checkDynamic, uint8_t sampleTimeSec)
{
    // Validar tiempo de muestreo
    if (sampleTimeSec < 2 || sampleTimeSec > 10) {
        sampleTimeSec = 2;
    }

    // Comando: 40 5A 10 00 SF ST 00 00
    uint8_t cmd[8] = {0x40, 0x5A, 0x10, 0x00, 
                      (uint8_t)(checkDynamic ? 1 : 0), 
                      sampleTimeSec, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::clearHeadingAngle()
{
    // Comando: 40 82 10 00 00 00 00 00
    uint8_t cmd[8] = {0x40, 0x82, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::setInstallationMode(VG55R_InstallMode mode)
{
    // Comando: 40 F3 10 00 ST 00 00 00
    uint8_t cmd[8] = {0x40, 0xF3, 0x10, 0x00, (uint8_t)mode, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

VG55R_Error VG55R_Driver::modifyNodeID(uint8_t newNodeID)
{
    // Validar rango
    if (newNodeID < 0x01 || newNodeID > 0x7F) {
        setError(VG55R_ERR_INVALID_NODEID);
        return VG55R_ERR_INVALID_NODEID;
    }

    // Comando: 40 10 10 00 NewID 00 00 00
    uint8_t cmd[8] = {0x40, 0x10, 0x10, 0x00, newNodeID, 0x00, 0x00, 0x00};
    
    VG55R_Error result = sendCommand(cmd);
    if (result == VG55R_OK) {
        // Actualizar IDs locales
        nodeID = newNodeID;
        dataID = 0x580 + nodeID;
        cmdID = 0x600 + nodeID;
    }
    return result;
}

VG55R_Error VG55R_Driver::setBaudRate(VG55R_BaudRate baudRate)
{
    // Comando: 40 20 10 00 ST 00 00 00
    uint8_t cmd[8] = {0x40, 0x20, 0x10, 0x00, (uint8_t)baudRate, 0x00, 0x00, 0x00};
    return sendCommand(cmd);
}

// ============================================================================
// ENVÍO DE COMANDOS
// ============================================================================

VG55R_Error VG55R_Driver::sendCommand(const uint8_t cmd[8])
{
    if (!mcp) {
        setError(VG55R_ERR_MCP_NULL);
        return VG55R_ERR_MCP_NULL;
    }

    struct can_frame frame;
    frame.can_id = cmdID;
    frame.can_dlc = 8;
    memcpy(frame.data, cmd, 8);

    MCP2515::ERROR result = mcp->sendMessage(&frame);

    if (result == MCP2515::ERROR_OK) {
        lastError = VG55R_OK;
        return VG55R_OK;
    } else {
        txErrorCount++;
        setError(VG55R_ERR_TX_FAILED);
        return VG55R_ERR_TX_FAILED;
    }
}