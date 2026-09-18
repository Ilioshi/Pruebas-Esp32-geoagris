# Modulos explicados

Referencia rapida para entender responsabilidades del sistema.

## 1. Orquestacion (main)

Proposito
- Inicializar runtime y crear tareas FreeRTOS segun flags.

Entradas
- Flags de compilacion y variables G_* de habilitacion.

Salidas
- Topologia activa de tareas por core.
- Inicializacion de canales base y colas.

Archivos clave
- src/main.cpp

## 2. SendTask

Proposito
- Agregador central y dispatcher de mensajes salientes STX.

Entradas
- Datos de Weather, Pressure, Pulse, CAN y Beacon mediante getData(out, seq).

Salidas
- Mensajes STX enviados por traxSendReceive.
- En simulacion, mensajes RTX15 via sender queue.
- En contexto Expoagro, actua como productor de datos simulados para envio rapido.

Archivos clave
- src/SendTask.cpp
- src/SendTaskWeather.cpp
- src/SendTaskPressure.cpp
- src/SendTaskPulse.cpp
- src/SendTaskCan.cpp
- src/SendTaskBeacon.cpp

## 3. Weather

Proposito
- Leer datos meteorologicos y GPS para envio.

Entradas
- WeatherStation y consultas GPS via trax_utils.

Salidas
- WeatherData protegido por mutex y weatherSeq.

Archivos clave
- src/WeatherTask.cpp
- src/WeatherStation.cpp
- src/WeatherCalculator.cpp

## 4. Pressure

Proposito
- Muestrear sensor de presion periodicamente.

Entradas
- PressureSensor y temporizacion interna.

Salidas
- PressureData y pressureSeq.

Archivos clave
- src/PressureTask.cpp
- src/PressureSensor.cpp

## 5. Pulse

Proposito
- Contar pulsos por ISR y convertir a tasas.

Entradas
- Interrupciones en GPIO14 y GPIO27.

Salidas
- PulseData con pulses, rate, totales y elapsedMs.
- pulseSeq para envio incremental.

Archivos clave
- src/Pulse.cpp

## 6. CAN core

Proposito
- Inicializar MCP2515, recibir frames y distribuir parseo.

Entradas
- Frames CAN desde interrupcion y polling.

Salidas
- Alimentacion de J1939 e ISOBUS.
- Estado VG55R para SendTask.

Archivos clave
- src/CAN/CAN.cpp
- include/CAN/CAN.h

## 7. Protocolos J1939 ISOBUS VG55R

Proposito
- Parsear payload CAN y normalizar datos por protocolo.

Entradas
- PGNs y payloads desde CAN.cpp.

Salidas
- J1939Data e IsobusData con timestamps y flags.
- Vg55rData.

Archivos clave
- src/CAN/CAN_protocols/j1939_protocol.cpp
- src/CAN/CAN_protocols/isobus_protocol.cpp
- src/CAN/CAN_protocols/VG55R.cpp

## 8. BleSerialTask

Proposito
- Puente BLE <-> serial y control de inactividad/keep alive.

Entradas
- Bytes BLE, bytes serial, estado de conexion.

Salidas
- Reenvio bidireccional BLE/serial.
- Eventos de estado.
- Canal operativo para OTA.

Archivos clave
- src/BleSerialTask.cpp
- src/AgxBle.cpp
- src/AgxSerial.cpp

## 9. WiFiTask

Proposito
- Puente UDP <-> serial para operacion por WiFi.

Entradas
- Estado WiFi, paquetes UDP, datos serial.

Salidas
- Reenvio serial a UDP y UDP a serial.
- Conmutacion TRAX IP0/TR1.

Archivos clave
- src/WiFiTask.cpp
- src/AgxWiFi.cpp
- src/trax_utils.cpp

## 10. trax_utils

Proposito
- Encapsular protocolo request/response con TRAX.

Entradas
- Comandos STX/QUS y datos de contexto.

Salidas
- Respuestas parseadas, datos GPS y utilidades de configuracion.

Archivos clave
- src/trax_utils.cpp
- include/trax_utils.h

## 11. Messanger_queue

Proposito
- Abstraer colas builder/sender para comunicacion entre tareas e ISR.

Entradas
- Mensajes char payload desde tareas o ISR.

Salidas
- Mensajes encolados/desencolados.
- Uso parcial hoy, principalmente en ruta de simulacion.
- Base recomendada para extender una arquitectura de colas general.

Notas de uso
- En Expoagro se uso como mecanismo practico para desacoplar simulacion y envio.
- Para ampliar el sistema de colas, conviene centralizar cambios en esta capa y mantener consumidores/productores conectados a su API.

Archivos clave
- src/Messanger_queue.cpp
- include/Messanger_queue.h

## 12. OtaSession

Proposito
- Gestionar sesion OTA sobre BLE y escritura de firmware.

Entradas
- Paquetes OTA recibidos por BLE.

Salidas
- ACK/NACK de protocolo OTA y reinicio en caso exitoso.

Archivos clave
- lib/OtaSession/otasession.cpp
- lib/OtaSession/README.md
