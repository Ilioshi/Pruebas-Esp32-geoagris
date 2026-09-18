/**
 * @file VG55R.h
 * @brief Driver para sensor de inclinación VG55R con interfaz CAN
 * @author Mariano Oms
 * @date 6/11/2025
 * 
 * Driver modular que asume que el bus CAN ya está configurado externamente.
 * Compatible con FreeRTOS tasks.
 */

#ifndef VG55R_DRIVER_H
#define VG55R_DRIVER_H

#include <Arduino.h>
#include <mcp2515.h>

// ============================================================================
// DEFINICIONES Y ESTRUCTURAS
// ============================================================================

/**
 * @brief Códigos de error del driver VG55R
 */
enum VG55R_Error {
    VG55R_OK                    = 0x00,  // Sin error
    VG55R_ERR_MCP_NULL          = 0x01,  // MCP2515 no inicializado
    VG55R_ERR_NOT_DETECTED      = 0x02,  // Sensor no detectado
    VG55R_ERR_TX_FAILED         = 0x03,  // Error al enviar mensaje CAN
    VG55R_ERR_INVALID_BCD       = 0x04,  // Datos BCD inválidos
    VG55R_ERR_INVALID_RANGE     = 0x05,  // Valor fuera de rango
    VG55R_ERR_NO_VALID_DATA     = 0x06,  // No hay datos válidos disponibles
    VG55R_ERR_TIMEOUT           = 0x07,  // Timeout de comunicación
    VG55R_ERR_INVALID_NODEID    = 0x08,  // NodeID fuera de rango
};

/**
 * @brief Datos de ángulos decodificados del sensor
 */
struct VG55R_Angles {
    float pitch;        // Ángulo de inclinación pitch (°)
    float roll;         // Ángulo de inclinación roll (°)
    float yaw;          // Ángulo de dirección yaw (°)
    uint32_t timestamp; // Timestamp de lectura (millis)
    bool valid;         // Indica si los datos son válidos
};

/**
 * @brief Datos de giroscopio decodificados
 */
struct VG55R_Gyro {
    float gyroX;        // Velocidad angular X (°/s)
    float gyroY;        // Velocidad angular Y (°/s)
    float gyroZ;        // Velocidad angular Z (°/s)
    uint32_t timestamp;
    bool valid;
};

/**
 * @brief Datos de acelerómetro decodificados
 */
struct VG55R_Accel {
    float accX;         // Aceleración X (g)
    float accY;         // Aceleración Y (g)
    float accZ;         // Aceleración Z (g)
    uint32_t timestamp;
    bool valid;
};

/**
 * @brief Datos de cuaternión decodificados
 */
struct VG55R_Quaternion {
    float q0;           // w
    float q1;           // x
    float q2;           // y
    float q3;           // z
    uint32_t timestamp;
    bool valid;
};

/**
 * @brief Frecuencias de salida automática disponibles
 */
enum VG55R_OutputFreq {
    VG55R_FREQ_ANSWER_MODE = 0x00,  // Modo respuesta (sin auto-output)
    VG55R_FREQ_5HZ         = 0x01,
    VG55R_FREQ_10HZ        = 0x02,
    VG55R_FREQ_20HZ        = 0x03,
    VG55R_FREQ_25HZ        = 0x04,
    VG55R_FREQ_50HZ        = 0x05,
    VG55R_FREQ_100HZ       = 0x06,  // Default
    VG55R_FREQ_200HZ       = 0x07,
    VG55R_FREQ_500HZ       = 0x08
};

/**
 * @brief Modos de datos automáticos (MINS/VG series)
 */
enum VG55R_AutoDataMode {
    VG55R_MODE_ANGLES           = 0x00,  // Ángulos 3 ejes
    VG55R_MODE_ACCEL            = 0x01,  // Acelerómetro 3 ejes
    VG55R_MODE_GYRO             = 0x02,  // Giroscopio 3 ejes
    VG55R_MODE_QUATERNION       = 0x03,  // Cuaternión
    VG55R_MODE_ALL_SHARED_ID    = 0x04,  // Todo en mismo ID (BCD)
    VG55R_MODE_ALL_DBC          = 0x05,  // Todo en DBC
    VG55R_MODE_QUAT_FLOAT       = 0x06,  // Cuaternión flotante
    VG55R_MODE_ALL_DIFF_ID      = 0x07   // Todo en IDs diferentes
};

/**
 * @brief Tipos de cero
 */
enum VG55R_ZeroType {
    VG55R_ZERO_ABSOLUTE = 0x00,
    VG55R_ZERO_RELATIVE = 0x01  // No incluye heading
};

/**
 * @brief Velocidades de baudrate CAN
 */
enum VG55R_BaudRate {
    VG55R_BAUD_500K = 0x01,
    VG55R_BAUD_250K = 0x02,
    VG55R_BAUD_125K = 0x03,  // Default
    VG55R_BAUD_100K = 0x04,
    VG55R_BAUD_50K  = 0x05,
    VG55R_BAUD_25K  = 0x06
};

/**
 * @brief Modos de instalación
 */
enum VG55R_InstallMode {
    VG55R_INSTALL_HORIZONTAL = 0x00,
    VG55R_INSTALL_VERTICAL   = 0x01
};

// ============================================================================
// CLASE DRIVER VG55R
// ============================================================================

class VG55R_Driver {
public:
    /**
     * @brief Constructor del driver
     * @param mcp Puntero al objeto MCP2515 ya inicializado
     * @param nodeID ID del nodo del sensor (default: 0x05)
     */
    VG55R_Driver(MCP2515* mcp, uint8_t nodeID = 0x05);

    /**
     * @brief Inicializa el driver (sin configurar el bus CAN)
     * @return Código de error (VG55R_OK si exitoso)
     */
    VG55R_Error begin();

    /**
     * @brief Inicia el proceso de auto-detección (no bloqueante)
     * @note Llamar a checkAutoDetect() periódicamente para verificar el estado
     */
    void startAutoDetect();

    /**
     * @brief Verifica el estado de la auto-detección
     * @param timeoutMs Tiempo máximo desde startAutoDetect() antes de timeout
     * @return VG55R_OK si detectado, VG55R_ERR_TIMEOUT si expiró, VG55R_ERR_NOT_DETECTED si aún buscando
     */
    VG55R_Error checkAutoDetect(uint32_t timeoutMs = 5000);

    /**
     * @brief Procesa frames CAN recibidos (llamar desde task de lectura)
     * @param frame Frame CAN recibido
     * @return true si el frame fue procesado por este driver
     */
    bool processFrame(const struct can_frame& frame);

    // ========================================================================
    // COMANDOS DE CONFIGURACIÓN
    // ========================================================================

    /**
     * @brief Solicita lectura del ángulo pitch (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestPitch();

    /**
     * @brief Solicita lectura del ángulo roll (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestRoll();

    /**
     * @brief Solicita lectura del ángulo yaw (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestYaw();

    /**
     * @brief Solicita lectura de los tres ángulos (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestAngles();

    /**
     * @brief Configura el tipo de cero
     * @param zeroType Tipo de cero a configurar
     * @return Código de error
     */
    VG55R_Error setZeroType(VG55R_ZeroType zeroType);

    /**
     * @brief Guarda la configuración actual en memoria no volátil
     * @note Esperar 4-5 segundos después de este comando antes de apagar
     * @return Código de error
     */
    VG55R_Error saveConfiguration();

    /**
     * @brief Configura la frecuencia de salida automática
     * @param freq Frecuencia deseada
     * @return Código de error
     */
    VG55R_Error setAutoOutputFrequency(VG55R_OutputFreq freq);

    /**
     * @brief Configura el modo de datos automático
     * @param mode Modo de datos deseado
     * @return Código de error
     */
    VG55R_Error setAutoDataMode(VG55R_AutoDataMode mode);

    /**
     * @brief Solicita lectura del giroscopio (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestGyroscope();

    /**
     * @brief Solicita lectura del acelerómetro (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestAccelerometer();

    /**
     * @brief Solicita lectura del cuaternión (no bloqueante)
     * @return Código de error
     */
    VG55R_Error requestQuaternion();

    /**
     * @brief Limpia el sesgo del giroscopio (comando nuevo)
     * @param checkDynamic true para verificar estado estático
     * @param sampleTimeSec Tiempo de muestreo (2-10 segundos)
     * @return Código de error
     */
    VG55R_Error clearGyroBias(bool checkDynamic = true, uint8_t sampleTimeSec = 2);

    /**
     * @brief Limpia el ángulo de heading (yaw)
     * @return Código de error
     */
    VG55R_Error clearHeadingAngle();

    /**
     * @brief Configura el modo de instalación
     * @param mode Modo de instalación (horizontal/vertical)
     * @return Código de error
     */
    VG55R_Error setInstallationMode(VG55R_InstallMode mode);

    /**
     * @brief Modifica el node ID del sensor
     * @param newNodeID Nuevo node ID (0x01-0x7F)
     * @return Código de error
     */
    VG55R_Error modifyNodeID(uint8_t newNodeID);

    /**
     * @brief Configura el baudrate del sensor
     * @param baudRate Baudrate deseado
     * @return Código de error
     */
    VG55R_Error setBaudRate(VG55R_BaudRate baudRate);

    // ========================================================================
    // ACCESO A DATOS (para lectura asíncrona desde tasks)
    // ========================================================================

    /**
     * @brief Obtiene los últimos ángulos recibidos
     * @return Estructura con los últimos ángulos válidos
     */
    VG55R_Angles getLastAngles() const { return lastAngles; }

    /**
     * @brief Obtiene los últimos datos del giroscopio
     * @return Estructura con los últimos datos válidos
     */
    VG55R_Gyro getLastGyro() const { return lastGyro; }

    /**
     * @brief Obtiene los últimos datos del acelerómetro
     * @return Estructura con los últimos datos válidos
     */
    VG55R_Accel getLastAccel() const { return lastAccel; }

    /**
     * @brief Obtiene el último cuaternión
     * @return Estructura con el último cuaternión válido
     */
    VG55R_Quaternion getLastQuaternion() const { return lastQuaternion; }

    /**
     * @brief Verifica si el sensor está detectado y comunicando
     * @param timeoutMs Timeout en ms para considerar desconectado (default: 3000ms)
     * @return true si hay comunicación activa
     */
    bool isConnected(uint32_t timeoutMs = 3000) const { 
        return detected && lastRxTime > 0 && (millis() - lastRxTime) < timeoutMs; 
    }

    /**
     * @brief Obtiene el período promedio entre frames (ms)
     * @return Período en milisegundos
     */
    uint32_t getFramePeriod() const { return framePeriod; }

    /**
     * @brief Obtiene el DATA_ID detectado del sensor
     * @return ID de datos del sensor (0x580 + nodeID)
     */
    uint16_t getDataID() const { return dataID; }

    /**
     * @brief Obtiene el CMD_ID para enviar comandos
     * @return ID de comandos (0x600 + nodeID)
     */
    uint16_t getCmdID() const { return cmdID; }

    /**
     * @brief Obtiene el NodeID del sensor
     * @return NodeID del sensor
     */
    uint8_t getNodeID() const { return nodeID; }

    /**
     * @brief Obtiene el último código de error
     * @return Último código de error registrado
     */
    VG55R_Error getLastError() const { return lastError; }

    /**
     * @brief Obtiene el contador de errores BCD
     * @return Cantidad de errores BCD detectados
     */
    uint32_t getBcdErrorCount() const { return bcdErrorCount; }

    /**
     * @brief Obtiene el contador de errores de transmisión
     * @return Cantidad de errores de TX CAN
     */
    uint32_t getTxErrorCount() const { return txErrorCount; }

    /**
     * @brief Resetea el estado de detección del driver
     * @note Útil para forzar una nueva detección
     */
    void resetDetection() { 
        detected = false; 
        lastRxTime = 0;
        framePeriod = 0;
        lastAngles.valid = false;
        lastGyro.valid = false;
        lastAccel.valid = false;
        lastQuaternion.valid = false;
        autoDetectStartTime = 0;
    }

    /**
     * @brief Verifica si el sensor está actualmente detectado
     * @return true si el sensor fue detectado
     */
    bool isDetected() const { return detected; }

    /**
     * @brief Resetea los contadores de error
     */
    void resetErrorCounters() {
        bcdErrorCount = 0;
        txErrorCount = 0;
        lastError = VG55R_OK;
    }

private:
    // Puntero al MCP2515 (manejado externamente)
    MCP2515* mcp;

    // IDs del sensor
    uint8_t nodeID;
    uint16_t dataID;  // 0x580 + nodeID
    uint16_t cmdID;   // 0x600 + nodeID

    // Estado de detección
    bool detected;
    uint32_t lastRxTime;
    uint32_t framePeriod;
    uint32_t autoDetectStartTime;  // Para auto-detección no bloqueante

    // Manejo de errores
    VG55R_Error lastError;
    uint32_t bcdErrorCount;
    uint32_t txErrorCount;

    // Datos recibidos (última lectura válida)
    VG55R_Angles lastAngles;
    VG55R_Gyro lastGyro;
    VG55R_Accel lastAccel;
    VG55R_Quaternion lastQuaternion;

    // Helpers de decodificación BCD
    static inline uint8_t nibbleHi(uint8_t b) { return (b >> 4) & 0x0F; }
    static inline uint8_t nibbleLo(uint8_t b) { return b & 0x0F; }

    // Decodificación de Pitch/Roll (3 bytes: SXXX.YY)
    static float decodePitchRoll(const uint8_t* data);

    // Decodificación de Yaw (2 bytes: XX.YY)
    static float decodeYaw(const uint8_t* data);

    // Decodificación de giroscopio (2 bytes: XXXX -> (XXXX-5000)/10)
    static float decodeGyro(const uint8_t* data);

    // Decodificación de acelerómetro (2 bytes: XXXX -> (XXXX-5000)/2500)
    static float decodeAccel(const uint8_t* data);

    // Decodificación de cuaternión (4 bytes: QSXYYYYY)
    static float decodeQuaternion(const uint8_t* data);

    // Envío de comando de 8 bytes
    VG55R_Error sendCommand(const uint8_t cmd[8]);

    // Registro de error
    void setError(VG55R_Error error) { lastError = error; }

    // Procesamiento de frames específicos
    void processAngleFrame(const uint8_t* data);
    void processGyroFrame(const uint8_t* data);
    void processAccelFrame(const uint8_t* data);
    void processQuaternionFrame(const uint8_t* data, bool firstHalf);
};

#endif // VG55R_DRIVER_H