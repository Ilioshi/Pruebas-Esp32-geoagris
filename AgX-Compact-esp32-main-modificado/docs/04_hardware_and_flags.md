# Hardware, buses y flags de compilacion

## 1. Mapa de pines relevantes

## 1.1 CAN MCP2515 (VSPI)

Definidos en `src/CAN/CAN.cpp`:
- CS: GPIO5
- SCK: GPIO18
- MISO: GPIO19
- MOSI: GPIO23
- INT: GPIO21

Configuracion de bus CAN:
- bitrate: 250 kbps (`CAN_250KBPS`)
- oscilador MCP: 8 MHz (`MCP_8MHZ`)

## 1.2 Pulsos

Definidos en `src/Pulse.cpp`:
- canal 1: GPIO14
- canal 2: GPIO27
- anti rebote por software: `DEBOUNCE_US = 500`

## 1.3 Serial hacia TRAX

Actual en `src/trax_utils.cpp`:
- usa `Serial` a 115200 para request/response
- protegido por semaforo global `G_semaphoreUART`

Nota: en `src/main.cpp` tambien se inicializa `AgxSerial::getInstance().begin()`.

## 2. Flags de compilacion principales

## 2.1 Flags funcionales

- `ENABLE_BLE`
  - habilita tarea BLE (`bleSerialTask`) y canal BLE

- `ENABLE_WIFI`
  - habilita tarea WiFi (`wifiTask`)

- `ENABLE_BEACON`
  - habilita escaneo y envio de beacon

- `ENABLE_PULSE`
  - habilita inicializacion de contadores de pulso

- `ENABLE_CAN_SNIFFER`
  - reemplaza `canReadTask` por `canSnifferTask`

- `ENABLE_SIMULATION`
  - habilita ruta de simulacion de clima via ISOBUS + sender queue

## 2.2 Flags tecnicas

- `CAN_USE_INTERRUPT` (default 1)
  - 1: interrupcion + polling de respaldo
  - 0: solo polling

- `CAN_DEBUG_ERRORS` (default 0)
  - 1: logs de debug en modulo CAN

- `DEBUG_WIFI`
  - activa trazas de debug de WiFi en entorno correspondiente

## 3. Relacion flags vs entornos

Referirse a `platformio.ini` para combinaciones oficiales.

Resumen:
- BLE puro: `esp32-PROD-BLE`
- WiFi puro: `esp32-PROD-WIFI`
- BLE + WiFi: `esp32-PROD-WIFI-BLE`
- variantes con beacon/pulse: entornos `...-BEACON`, `...-PULSE`
- debugging: entornos `esp32-DEB-*`

## 4. Buenas practicas de hardware

- usar terminaciones CAN correctas (120 ohm en extremos)
- validar alimentacion estable de MCP2515
- minimizar cables largos en lineas SPI
- confirmar masa comun entre nodos

## 5. Checklist de integracion rapida

1. Verificar pines cableados segun seccion 1.
2. Confirmar entorno correcto segun flags requeridas.
3. Confirmar actividad de bus CAN antes de test de parseo.
4. Confirmar respuesta TRAX para `QUS07` si se usa GPS.
