# Hardware test de las placas ESP32

Este proyecto contiene solo el firmware de prueba de dos placas ESP32. Se carga el mismo `src/main.cpp` en ambas; el entorno de PlatformIO define `DEVICE_ID=1` (placa de referencia) o `DEVICE_ID=2` (placa bajo prueba). Ambas placas deben llevar esta version para compartir resultados y capturas.

## Que comprueba

| Canal | Pines de la ESP32 | Prueba |
| --- | --- | --- |
| TTL1 | RX GPIO25, TX GPIO26; 115200 baud | Cada placa envia PING y responde PONG a la otra. |
| TTL2 | RX GPIO3, TX GPIO1; 115200 baud | Igual que TTL1; comparte UART0 con el USB. |
| RS485 | RX GPIO16, TX GPIO17, DE/RE GPIO4; 9600 baud | Placa 1 envia PING y placa 2 responde PONG. Ambas envian informes. |
| CAN | MCP2515: CS5, SCK18, MISO19, MOSI23; 250 kbit/s, cristal 8 MHz | Cada placa envia una trama de prueba y cuatro tramas de informe. |
| Pulsos | GPIO27 y GPIO35 **de placa 2** | Flancos ascendentes; p/s del ultimo intervalo de 1 segundo y contador del ultimo bloque completo de 10 segundos. |

Los resultados salen por Bluetooth clasico SPP (`HWTEST-ESP32-1` y `HWTEST-ESP32-2`), no por BLE. La placa 1 se considera una referencia conocida al evaluar la placa 2. Un resultado localiza el trayecto donde falla la prueba; por si solo no demuestra cual componente fisico se averio.

## Carga y conexiones

Abrir esta carpeta `hardware-test` como proyecto de PlatformIO. Seleccionar expresamente el entorno al cargar: el `default_envs` de `platformio.ini` puede cambiar.

```powershell
pio run -e esp32-hardware-test -t upload --upload-port COM7
pio run -e esp32-hardware-test-2 -t upload --upload-port COM8
```

Reemplazar `COM7` y `COM8` por los puertos reales. `esp32-hardware-test` compila la placa 1; `esp32-hardware-test-2`, la placa 2. No hay que editar `main.cpp` ni comentar y descomentar una linea para cambiar de placa.

Conectar TTL1 TX26 de cada placa al RX25 de la otra; TTL2 TX1 de cada placa al RX3 de la otra; masa comun. En RS485 conectar A con A, B con B y masa comun. En CAN conectar H con H, L con L y masa comun. Usar terminacion de 120 ohm en los extremos de cada bus RS485 y CAN, segun el montaje. Para TTL2, desconectar el USB durante la prueba: GPIO1 y GPIO3 son UART0 y el USB puede interferir. GPIO35 no tiene pull-up interno; aplicarle una senal externa adecuada. Las entradas de pulsos se prueban solo en placa 2.

## Uso por Bluetooth

Las pruebas empiezan al encender. Con una aplicacion de terminal Bluetooth **clasico SPP**, conectarse a cualquiera de las dos placas y enviar el comando seguido de salto de linea (`LF` o `CR+LF`). Se aceptan mayusculas y minusculas. El resumen aparece automaticamente cada 10 segundos. Las pruebas de enlace se envian cada 2 segundos; el plazo de respuesta PONG es de 600 ms. Los p/s se actualizan cada segundo aunque el resumen automatico conserve su frecuencia de 10 segundos.

| Comando | Resultado |
| --- | --- |
| `STATUS` | Muestra el resumen del ultimo bloque de 10 segundos y la ultima medicion disponible de p/s de 1 segundo. |
| `DATOS` o `DATA` | Detalle por canal de ambas placas y matriz compacta de informes `INFO`. |
| `TTL1`, `TTL2`, `CAN` | Diagnostico extendido del canal: estado de cada placa, comparacion de la ultima trama de prueba, TX/RX crudos y las cuatro copias `INFO` recibidas por ese canal. |
| `RS485` | Mismo detalle por canal: ambas placas, comparacion de prueba, TX/RX crudos y copias `INFO`. Consultado desde placa 1, agrega la captura del envio de informe RS485 de placa 2. |
| `RAW` | Ultimas muestras RX de ambas placas para TTL1, TTL2, RS485 y CAN; incluye datos invalidos y auxiliares. |
| `RUN`, `STOP` | Reanuda o detiene el envio de pruebas automaticas de la placa conectada (PING y tramas de prueba CAN). |
| `HELP` | Lista los comandos. |

`DATOS` separa los cuatro canales, muestra el estado de cada placa y deja los informes 1->2 y 2->1 en dos lineas breves, sin encabezados repetidos. Una sola seccion `COPIAS INFO` compara las copias del ultimo informe remoto. Los comandos con nombre de canal agregan las tramas y lecturas crudas. Los numeros de las dos placas pueden variar en una misma ventana porque sus relojes y cierres de 10 segundos no estan sincronizados.

`STOP` no detiene la recepcion, las respuestas a PING, los informes ni la medicion de pulsos. Tampoco detiene la otra placa. `RUN` reanuda los envios sin reiniciar los contadores ni cerrar una ventana nueva.

Cada receptor confirma por TTL1 las copias `INFO` que reconocio en cada canal. Una confirmacion pendiente no se interpreta como fallo del canal. Los informes nuevos tienen un margen de 3,5 segundos antes de marcar una copia faltante como `NO LLEGA`, tambien en los comandos individuales.

## Como leer los resultados

- En TTL1/TTL2, `PING enviados` cuenta los pedidos propios; `PONG confirmados`, sus respuestas correctas. `PING del otro recibidos` y `PONG enviados` miden la direccion inversa. Un PING al borde de una ventana puede figurar pendiente y cerrarse en la siguiente.
- En RS485, solo placa 1 inicia PING. Si placa 2 no reconoce ese PING, no tiene motivo para generar PONG. El informe RS485 de placa 2 se intenta por separado aunque falle PING/PONG. `RS485` distingue los bytes escritos en cada UART de los que realmente leyo la otra placa; escribir en UART no prueba que haya senal correcta en A/B.
- `<00>` es un byte NUL recibido por la UART RS485, no un PONG. Un NUL aislado puede coincidir con la conmutacion del transceptor y no se atribuye automaticamente a una placa. Si no llega una trama valida, `RS485` muestra la muestra cruda y el esperado cuando existe una captura comparable. Para saber su origen electrico hacen falta mediciones del bus y transceptores.
- En CAN no hay PONG: cada placa envia una trama y la otra comprueba ID, DLC y datos. `buffer RX lleno` significa que el MCP2515 no pudo guardar alguna trama; su contenido perdido no puede mostrarse como `recibido`. Las cuatro tramas de informe CAN se espacian para reducir este riesgo.
- `COPIAS INFO` compara el mismo informe de la otra placa recibido por TTL1, TTL2, RS485 y CAN. `REFERENCIA`/`REF` es la primera copia disponible, `IGUAL` coincide con ella, `DIFERENTE` no coincide, `EN CAMINO` aun puede llegar y `NO LLEGA` indica que no se reconocio una copia valida. Una primera copia no prueba por si sola que su contenido sea correcto.

## Pulsos: p/s de 1 segundo y contador de 10 segundos

Solo la placa 2 mide las entradas GPIO27 y GPIO35. Son los mismos GPIO definidos en el codigo del firmware de produccion revisado; son numeros de GPIO de la ESP32, no numeros de posicion de un conector.

- `Pulsos placa 2 (1 s)`: tasa del ultimo intervalo de muestreo completado, actualizado aproximadamente cada 1000 ms. Se calcula con los pulsos nuevos de ese intervalo y su duracion real, con una cifra decimal. No usa el promedio de los 10 segundos ni el tiempo desde el encendido.
- `Contador (10 s)`: cantidad de pulsos del ultimo bloque completo de 10 segundos. Se reemplaza al cerrar cada bloque; no es un acumulado desde el encendido ni el conteo parcial del bloque en curso.

La placa 2 comparte ambos valores por TTL1 cada segundo y al enviar su informe de 10 segundos. `STATUS` y `DATOS` muestran los ultimos valores disponibles. Al consultar placa 1, si no llega una actualizacion de pulsos durante mas de 2,5 segundos, los p/s indican `sin dato reciente`. El contador se muestra solo si corresponde al informe remoto vigente y hay datos recientes (hasta 15 segundos). Antes de completar el primer bloque, aparece `Contador (10 s): sin ventana reciente completa`.

Con una senal estable de 1000 Hz en GPIO27 durante un bloque completo, y GPIO35 sin pulsos, se espera aproximadamente:

```text
Pulsos placa 2 (1 s): GPIO27=1000.0 p/s; GPIO35=0.0 p/s
  Contador (10 s): GPIO27=10000 GPIO35=0
```

Si se corta la senal, los p/s pasan a cero cuando se completa un intervalo de muestreo sin pulsos. El contador conserva el ultimo bloque cerrado hasta que termine el siguiente. Por eso puede mostrar pulsos aunque los p/s ya sean cero.

GPIO27 se configura con pull-up interno y GPIO35 como entrada sin pull-up interno, porque este ultimo no dispone de esa funcion. Si la salida del equipo necesita pull-up, debe existir externamente. Para un generador conectado directamente a la ESP32, unir su salida a GPIO27 o GPIO35 y compartir GND. El GPIO33 del generador de prueba es su salida; las entradas del hardware test siguen siendo GPIO27 y GPIO35.

## Capturas crudas y limites

Las capturas UART conservan los ultimos 1024 bytes por placa, canal y direccion de cada ventana; CAN conserva las ultimas 32 tramas por direccion con ID, DLC y datos. `RAW` muestra todo lo conservado en RX, incluidos NUL y mensajes auxiliares; los bytes UART aparecen en HEX y TXT. Si una ventana excede el limite, aparece `RECORTADO` y cuantos datos se conservaron. No es un registro ilimitado desde el encendido. El TX UART muestra PING/PONG/INFO; el RX incluye tambien la comunicacion auxiliar de diagnostico.

Cada muestra identifica su informe. Las ventanas de ambas placas no son simultaneas; no deben compararse sus muestras byte a byte como si correspondieran al mismo instante. La muestra remota viaja en fragmentos por TTL1 y solo se publica cuando esta completa. Un CRC detecta alteraciones en los mensajes auxiliares; una comparacion remota etiquetada `CORRECTA` tambien debe pasar una validacion del contenido. Si falta una muestra o se rechaza un mensaje auxiliar, se indica sin atribuirle una falla al canal ensayado. Cargar esta version en ambas placas es necesario para este protocolo.

RS485 conserva ademas una captura de hasta 128 bytes asociada al intento de informe de placa 2, independiente de la muestra general de su ventana. Los limites y cualquier recorte se indican. Los envios de diagnostico por TTL1 y la salida Bluetooth se dosifican para que el bucle siga atendiendo las recepciones.
