# Firmware de prueba de hardware

Un unico firmware para las dos placas. El codigo esta en `src/main.cpp`; la unica diferencia entre placas es `DEVICE_ID`, definido por el entorno de PlatformIO.

## Que prueba

- TTL1 bidireccional: UART2, RX25/TX26, 115200.
- TTL2 bidireccional: UART0, RX3/TX1, 115200.
- RS485 bidireccional: UART1, RX16/TX17, DE/RE4, 9600.
- CAN MCP2515: CS5, SCK18, MISO19, MOSI23, 250 kbps, cristal 8 MHz.
- Pulsos: GPIO27 y GPIO35.
- Consola de diagnostico por Bluetooth clasico SPP.

Cada placa envia `PING` y espera `PONG` por TTL1, TTL2 y CAN. En RS485 la placa 1 inicia la prueba y la placa 2 responde. Los resultados se informan localmente por Bluetooth, por lo que un fallo de un enlace no impide ver el diagnostico.

## Preparar la carga

Abrir en VS Code la carpeta `hardware-test`. Desde la terminal integrada, PlatformIO puede ejecutarse con `pio`; si no aparece en el PATH, usar:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
```

El firmware no se carga desde `AgX-Compact-esp32-main-modificado`: esa carpeta es la copia del firmware original y queda como referencia.

## Cargar las placas

Usar el mismo cable USB y el mismo codigo para ambas placas:

1. Conectar la primera placa y seleccionar su puerto COM.
2. Cargar el entorno de placa 1:

```powershell
pio run -e esp32-hardware-test -t upload --upload-port COM7
```

3. Desconectar la primera placa, conectar la segunda y reemplazar `COM7` por su puerto.
4. Cargar el entorno de placa 2:

```powershell
pio run -e esp32-hardware-test-2 -t upload --upload-port COM8
```

Los entornos solo cambian el numero compilado:

```text
esp32-hardware-test    -> DEVICE_ID=1 -> HWTEST-ESP32-1
esp32-hardware-test-2  -> DEVICE_ID=2 -> HWTEST-ESP32-2
```

No hay dos archivos `main.cpp` ni dos programas distintos.

## Conexion entre placas

- TTL1: TX26 de una placa a RX25 de la otra, RX26 a TX25 y masa comun.
- TTL2: TX1 de una placa a RX3 de la otra, RX3 a TX1 y masa comun.
- RS485: A con A, B con B y masa comun; terminacion de 120 ohm en los extremos.
- CAN: CANH con CANH, CANL con CANL y masa comun; terminacion de 120 ohm en los extremos.
- Pulsos: aplicar una señal de prueba a GPIO27 y GPIO35 de cada placa.

TTL2 usa UART0 y comparte GPIO1/3 con el USB. Se puede usar el USB para cargar, pero debe desconectarse durante la prueba de TTL2. La consola de resultados se usa por Bluetooth.

GPIO35 es entrada solamente y no tiene pull-up interno; necesita una señal externa correctamente polarizada.

## Bluetooth: clasico SPP, no BLE

El firmware usa `BluetoothSerial`, que corresponde a Bluetooth clasico con perfil SPP (puerto serie). No usa BLE/GATT. Por eso hay que buscar los dispositivos desde una aplicacion compatible con Bluetooth Serial/SPP.

## Usar Bluetooth Serial Terminal

1. Cargar ambas placas y conectar todos los cables de prueba.
2. Alimentar las placas y esperar unos segundos.
3. Abrir una aplicacion compatible con Bluetooth clasico SPP, por ejemplo Bluetooth Serial Terminal.
4. Conectarse a `HWTEST-ESP32-1` o `HWTEST-ESP32-2`.
5. Las pruebas empiezan automaticamente al arrancar: no hace falta enviar `RUN` para iniciar la prueba.
6. Enviar los comandos terminados en salto de linea:

```text
STATUS
HELP
STOP
RUN
```

El estado tambien se envia automaticamente cada cinco segundos.

- `STATUS`: muestra el estado actual sin cambiar la prueba.
- `RUN`: reanuda las pruebas automaticas si estaban detenidas.
- `STOP`: detiene los nuevos `PING`; mantiene disponible `STATUS`.
- `HELP`: muestra los comandos disponibles.

El estado indica `RUNNING` o `STOPPED`. Para la prueba normal solo hay que conectar ambas placas y observar el estado; `STOP` y `RUN` son utiles para aislar un enlace o repetir una prueba.

## Interpretar el resultado

Ejemplo:

```text
--- HWTEST placa 1 ---
TTL1: tx=10 rx=10 ok=10 err=0
TTL2: tx=10 rx=10 ok=10 err=0
RS485: tx=10 rx=10 ok=10 err=0
CAN: tx=10 rx=10 ok=10 err=0
PULSOS: pin27=25 pin35=25
```

- `tx`: mensajes enviados por ese canal.
- `rx`: mensajes recibidos.
- `ok`: respuestas o tramas validas del otro nodo.
- `err`: timeouts o respuestas invalidas.
- En `PULSOS`, los valores son los conteos acumulados desde el arranque.

Un canal con `tx` aumentando y `rx=0` indica que la placa local transmite, pero no recibe respuesta. Un `tx` que no aumenta puede indicar que el canal no esta siendo iniciado, un problema de configuracion o un cableado incorrecto.
