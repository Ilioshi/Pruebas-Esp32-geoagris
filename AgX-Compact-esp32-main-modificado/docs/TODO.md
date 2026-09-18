# TODO del proyecto

Checklist priorizado con pendientes tecnicos y documentales.

## 1. Documentacion

- [ ] [Alta] Mantener sincronizado el diagrama de bloques con cambios en main y tasks.
- [ ] [Media] Incorporar ejemplos reales de tramas STX en protocolos y mensajes.
- [ ] [Media] Agregar matriz de responsabilidad por modulo y owner tecnico.
- [ ] [Baja] Agregar glosario de terminos operativos (TRAX, STX, PGN, ISR, etc.).

## 2. Arquitectura de mensajeria

- [ ] [Alta] Definir if de compilacion limpio en src/main.cpp.
- [ ] [Media] Emprolijar inicializaciones y moverlas a tareas/modulos en src/main.cpp.
- [ ] [Alta] Limpiar implementacion VG55R en src/CAN/CAN.cpp.
- [ ] [Alta] Sacar canGetVg55rData de CAN.cpp y mover a modulo dedicado en src/CAN/CAN.cpp.
- [ ] [Alta] Crear entorno PlatformIO CAN + BLE + WiFi pendiente en platformio.ini.

## 3. Confiabilidad y telemetria

- [ ] [Alta] Revisar validaciones incompletas en src/WeatherTask.cpp.
- [ ] [Media] Migrar trax_utils para usar AgxSerial en src/trax_utils.cpp.
- [ ] [Media] Reasignar codigo no ISOBUS a J1939 en src/CAN/CAN_protocols/isobus_protocol.cpp.
- [ ] [Media] Refactorizar ruta de simulacion en src/CAN/CAN_protocols/isobus_protocol.cpp.
- [ ] [Media] Optimizar memoria dinamica en BeaconTask en src/BeaconTask.cpp.
- [ ] [Media] Evaluar acumulador local en BeaconTask en src/BeaconTask.cpp.

## 4. Testing

- [ ] [Alta] Agregar pruebas unitarias de builders STX para Weather, Pressure, Pulse y CAN.
- [ ] [Alta] Agregar prueba de regresion para parseo J1939 e ISOBUS con frames reales.
- [ ] [Media] Crear pruebas de carga para flujo SendTask y timeouts de traxSendReceive.
- [ ] [Media] Definir smoke test OTA BLE automatizable para firmware de prueba.
