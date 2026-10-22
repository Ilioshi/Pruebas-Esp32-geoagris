# Hardware test de placas ESP32 — protocolo v2

Firmware para placa 1 de referencia y placa 2 bajo prueba. **Cargar esta versión en ambas placas**: v2 cambia los informes y el transporte de diagnóstico.

## Conexiones y carga

| Canal | GPIO y configuración |
| --- | --- |
| TTL1 | RX25 / TX26, 115200 baud, 8N1 |
| TTL2 | RX3 / TX1, 115200 baud, 8N1; comparte UART0 con USB |
| RS485 | RX16 / TX17 / DE-RE4, 9600 baud, 8N1 |
| CAN | MCP2515: CS5, SCK18, MISO19, MOSI23; 250 kbit/s, cristal 8 MHz |
| Pulsos de placa 2 | GPIO27 con pull-up interno y GPIO35 sin pull-up interno; flanco ascendente |

Son GPIO, no posiciones de conectores. Cruzar TX y RX entre placas para cada TTL y compartir GND. RS485: A con A y B con B. CAN: H con H y L con L. Compartir masa y colocar terminaciones de 120 Ω en los extremos de cada bus según el montaje, considerando las ya incorporadas.

Usar alimentación y niveles compatibles con el hardware. Desconectar USB durante la prueba de TTL2. GPIO35 necesita polarización externa si la salida del generador o sensor la requiere.

Abrir esta carpeta en PlatformIO y seleccionar el entorno explícitamente:

```powershell
pio run -e esp32-hardware-test -t upload --upload-port COM7
pio run -e esp32-hardware-test-2 -t upload --upload-port COM8
```

Los puertos son ejemplos. El primer entorno define `DEVICE_ID=1` y el segundo `DEVICE_ID=2`; `default_envs` no identifica la placa conectada.

## Resultados locales

Las pruebas comienzan al encender. Conectar una terminal **Bluetooth clásico SPP**, no BLE, a `HWTEST-ESP32-1` o `HWTEST-ESP32-2`. Enviar comandos con LF o CR+LF; se aceptan mayúsculas y minúsculas.

`STATUS` presenta el resultado local de la última ventana cerrada, sin necesitar informes remotos para interpretarlo. Las ventanas son de aproximadamente 10 s y el resumen automático se imprime después del cierre. Antes del primer cierre todavía no hay una ventana evaluable.

TTL1 y TTL2 inician PING desde ambas placas y verifican PONG. RS485 inicia PING desde placa 1 y responde PONG desde placa 2. El intervalo de prueba es 2 s y el plazo UART es 600 ms; los envíos RS485 se coordinan para evitar solapamientos con informes y diagnóstico. CAN transmite desde ambas placas y valida ID, DLC, emisor y marcador; no utiliza PONG ni verifica continuidad de todas las secuencias.

Un PONG correcto confirma ida y vuelta. Si falta, por sí solo no identifica qué sentido falló. Un `OK` aislado no equivale a aprobación final: revisar errores, detalle y condiciones del ensayo.

## Diagnóstico sin dependencia exclusiva de TTL1

Placa 1 sondea TTL1, TTL2, RS485 y CAN con mensajes protegidos por CRC y respuestas identificadas. Una vía queda disponible para diagnóstico después de confirmar un intercambio de ida y vuelta. Esto sigue activo con `STOP`.

Para elegir una vía, la prioridad es TTL1, TTL2, CAN y RS485. Se conserva la elegida mientras responda recientemente y no acumule dos fallas consecutivas. Si falla, se usa otra comprobada; si no queda ninguna, se siguen sondeando las cuatro. `DATOS`/`DATA` y el diagnóstico detallado de cada canal muestran la vía elegida. El resumen automático y `STATUS` muestran los canales y pulsos. Esta vía transporta información: no constituye una referencia independiente de exactitud.

El transporte confirma secuencias, conserva mensajes pendientes y evita aplicar dos veces un reintento incluso al cambiar de canal. Un identificador de sesión distingue reinicios. CAN fragmenta el mensaje y verifica su CRC completo. Placa 1 arbitra los intercambios, también para RS485 half duplex.

Si todos los canales fallan, los resultados locales siguen disponibles. Consultar placa 2 por su propio Bluetooth para obtener su evidencia.

## Informes redundantes

Cada placa envía los cuatro registros de su ventana por las cuatro vías. UART agrega un CRC; CAN envía datos y un CRC asociado por informe y canal. Solo se acepta una copia con CRC válido. RS485 solicita los informes de placa 2 por el propio bus mediante `IREQ` y reserva un turno para recibirlos; no necesita TTL1.

La copia válida recibida por la **vía auxiliar seleccionada** es la referencia de comparación del mismo informe. Si el auxiliar es TTL2, `REF` aparece en la fila TTL2 aunque TTL1 haya entregado antes una copia válida. `REF`/`REFERENCIA AUXILIAR`, `IGUAL` y `DIFERENTE` describen esta comparación, sin garantizar por sí solos la exactitud del contenido. Si falta la copia reciente del mismo informe por el auxiliar, las copias recibidas indican `SIN REF`/`SIN REFERENCIA AUXILIAR`: no se toma otra vía como referencia automáticamente.

`EN CAMINO` permite 3,5 s desde la primera copia de un informe. `NO LLEGA` indica que no se reconoció una copia válida después del margen. Una confirmación auxiliar pendiente no demuestra una falla del canal ensayado. Las ventanas de las placas no están sincronizadas; sus contadores pueden diferir.

## Comandos y capturas a pedido

| Comando | Resultado |
| --- | --- |
| `HELP` | Ayuda. |
| `STATUS` | Resumen local de canales y pulsos disponibles. |
| `DATOS` / `DATA` | Vía auxiliar, contadores de ambas placas, informes y comparación de copias. |
| `TTL1`, `TTL2`, `RS485`, `CAN` | Muestra el detalle disponible y solicita una captura remota del canal. |
| `RAW` | Muestra RX disponible y solicita las capturas remotas de los cuatro canales. |
| `STOP` / `RUN` | Detiene/reanuda envíos locales de prueba sin reiniciar contadores. |

Las capturas grandes se transfieren solo a pedido, con menor prioridad que pulsos y resúmenes. Al completar la transferencia se imprime automáticamente el resultado actualizado. Puede solicitarse desde cualquiera de las placas; solo se mantiene una solicitud local pendiente a la vez. Si no completa en 120 s, se informa para poder repetirla. CAN y RS485 pueden tardar más que TTL.

Se conserva una captura remota hasta 120 s desde su recepción, con su identificador de informe. Puede ser anterior a los contadores actuales. No comparar ventanas diferentes como si fueran simultáneas.

Límites por placa, canal y dirección: últimos 1024 bytes UART o últimas 32 tramas CAN. RS485 agrega hasta 128 bytes del intento de informe 2→1. `RECORTADO` señala que no se conserva toda la ventana. **Los mensajes auxiliares DX se excluyen de las capturas y contadores de prueba**, evitando encapsular capturas dentro de otras capturas. Los datos de prueba e informes nativos sí se incluyen. TX UART documenta escritura en UART, no una medición eléctrica.

`STOP` mantiene recepción, respuestas, informes, diagnóstico y pulsos; tampoco detiene la otra placa.

## Pulsos

Solo placa 2 mide GPIO27 y GPIO35. La tasa usa el último intervalo de aproximadamente 1 s y su duración real, con una cifra decimal. El contador usa el último bloque completo de aproximadamente 10 s; no es acumulado desde el arranque.

Ejemplo: 1000 Hz estables en GPIO27 durante todo un bloque, con GPIO35 sin pulsos y en un estado definido, producen aproximadamente 1000,0 p/s y 10000 pulsos. Esto no establece tolerancias ni frecuencia máxima.

Al retirar la señal, la tasa pasa a cero al completar un intervalo sin pulsos y el contador conserva el bloque anterior hasta el próximo cierre. Las muestras remotas viajan por la vía elegida. Si pasan más de 2,5 s sin actualizarse, placa 1 indica `sin dato reciente`; esto puede ocurrir transitoriamente durante una recuperación de enlace. El contador remoto requiere datos vigentes del informe correspondiente, hasta 15 s.

## Interpretación y validación

- Un PING al borde de una ventana puede resolverse en la siguiente.
- NUL en RS485 no equivale a PONG ni identifica un componente averiado. Revisar cableado, capturas y mediciones del bus.
- `buffer CAN RX lleno` indica pérdida potencial de tramas en MCP2515. El tráfico auxiliar también carga el controlador.
- Datos auxiliares incompletos, CRC rechazados y mensajes omitidos se informan separadamente de las pruebas.
- Los resultados localizan trayectos con problemas; no certifican toda la placa ni un componente específico.

Compilar ambos entornos con `pio run -e esp32-hardware-test -e esp32-hardware-test-2`.

`python tests/run_host_tests.py RUTA_A_ZIG` compila el código real de ambas placas con periféricos simulados. Las pruebas cubren vías aisladas, desconexión de TTL1, comunicación en un sentido, CRC incorrecto, pérdidas y reinicio de sesión. Zig es necesario solo para estas pruebas de escritorio, no para compilar o cargar el firmware con PlatformIO.

El simulador no reproduce niveles eléctricos, interrupciones, arbitraje físico ni tiempos reales de UART. Antes de adoptar v2 en fabricación, verificar en banco: desconectar TTL1 manteniendo diagnóstico; ensayar solo TTL2/RS485/CAN; comprobar pulsos y RAW en ambos sentidos; revisar tiempos y desbordamientos CAN.
