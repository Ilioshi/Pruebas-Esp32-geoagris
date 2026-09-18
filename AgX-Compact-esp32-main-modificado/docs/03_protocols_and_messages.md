# Protocolos y formatos de mensaje

Documento de referencia de los formatos salientes y del canal TRAX/OTA.

## 1. Canal TRAX

Interfaz publica:
- `traxSendReceive(const std::string& data)` en `src/trax_utils.cpp`

Comportamiento:
- Envia mensaje por serial
- Espera respuesta hasta 1000 ms
- Si la respuesta contiene `?`, intenta password `>SPWgeo18ris<` y reintenta comando

Funciones de apoyo:
- `traxGpsSpeedKmh()`
- `traxGpsHeading()`
- `traxGpsTimeString()`
- `traxGetWifiCredentialsCount(...)`
- `traxSetIP0()` / `traxSetTR1()`

## 2. Mensajes salientes construidos por SendTask

## 2.1 Clima

`SendTaskWeather`:
- `buildStx06(const WeatherData&)`
- `buildStx15(const WeatherData&)`

Formato STX06:

```text
>STX06,HAN:<payload_base64><
```

Campos incluidos (base64 url-safe):
- temperatura aire
- humedad relativa
- presion
- viento (m/s)
- direccion de viento
- compas
- precipitacion

STX15 se construye con `WeatherCalculator::buildSTX15Message(...)`.

## 2.2 Presion

`SendTaskPressure::buildStx09(const PressureData&)`

```text
>STX09,P<valor_base64><
```

Si no hay dato valido:

```text
>STX09,P~~<
```

## 2.3 Pulsos

`SendTaskPulse::buildStx13(const PulseData&)`

```text
>STX13,PLS:<rate1>:<rate2>:<total1>:<total2><
```

## 2.4 CAN J1939

`SendTaskCanJ1939::buildMessage(...)`

```text
>STX08,<rpm><torque><fuel><temp><hours><
```

- Cada campo se emite con ancho fijo
- Si un campo no esta fresco/valido, se usa placeholder
- frescura tipica: 5000 ms (horas: 3600000 ms)

## 2.5 CAN ISOBUS

`SendTaskCanIsobus` produce dos mensajes:

1. `buildMessage1` -> STX03 `IS1`
2. `buildMessage2` -> STX13 `IS2`

Ejemplo conceptual:

```text
>STX03,IS1:<datetime>:<lat>:<lon>:...<
>STX13,IS2:<datetime>:<setpoints>:<actuales>:...<
```

## 2.6 VG55R

`SendTaskCanVg55r::buildMessage(...)`

```text
>STX13,VG5:<pitch>:<roll>:<yaw>:<timestamp><
```

## 2.7 Beacon

`SendTaskBeacon::buildStx13(const BeaconReportData&)`

```text
>STX13,BCS:<mac_sin_dos_puntos>:<distancia>:...<
```

## 3. Protocolo OTA BLE

Implementacion:
- libreria `lib/OtaSession`
- cliente de ejemplo `scripts/ble_ota.py`

Paquetes principales (resumen):
- Cliente -> ESP32: `>OTADATA,<id>,<len>,<raw_bytes><`
- Cliente -> ESP32: `>OTAFINISH<`
- ESP32 -> Cliente: `>OTAOK<id><`, `>OTACURRENT<id><`, `>OTAFAILED<id><`
- Resultado final: `>OTAFINISH<id><` o estado de error/corrupcion

Notas:
- payload OTA fijado en 480 bytes
- sesion OTA se crea con `OtaSession::begin(...)`
- si termina ok, el dispositivo reinicia automaticamente

## 4. Comandos especiales

- `>ESPRESTART<` reinicia el ESP32

## 5. Reglas para agregar un mensaje nuevo

1. Definir estructura de datos y validez en su modulo productor.
2. Exponer `getData(out, seq)` thread-safe.
3. Implementar builder en `SendTask*.cpp/.h`.
4. Integrar en `sendTask` con criterio de frescura y rate.
5. Documentar formato, placeholders y timeouts en este archivo.
