# Pruebas-Esp32-geoagris

Firmware de diagnóstico para comprobar las comunicaciones y entradas de pulsos de dos placas ESP32. Una placa funciona como referencia conocida y la otra como placa bajo prueba. Los resultados se consultan desde una terminal Bluetooth clásico SPP.

El proyecto permite localizar en qué canal y sentido falla la comunicación, comparar los datos esperados y recibidos y consultar capturas crudas de ambas placas. Los resultados no identifican por sí solos el componente físico averiado.

## Qué se prueba

| Canal | Prueba |
| --- | --- |
| TTL1 y TTL2 | Envío de PING y respuesta PONG en ambos sentidos. |
| RS485 | PING desde placa 1 y PONG desde placa 2; informes enviados por ambas placas. |
| CAN | Tramas de prueba e informes mediante MCP2515. |
| Pulsos | Entradas GPIO27 y GPIO35 de placa 2: p/s del último intervalo de 1 segundo y contador del último bloque completo de 10 segundos. |

## Empezar

1. Abrir la carpeta [`hardware-test`](hardware-test/) con PlatformIO.
2. Revisar los pines y conexiones en la [guía del hardware test](hardware-test/README.md#carga-y-conexiones).
3. Cargar el mismo código en ambas placas, seleccionando el entorno correspondiente a cada una.

Desde la raíz del repositorio:

```powershell
# Placa 1: referencia
pio run -d hardware-test -e esp32-hardware-test -t upload --upload-port COM7

# Placa 2: bajo prueba
pio run -d hardware-test -e esp32-hardware-test-2 -t upload --upload-port COM8
```

Reemplazar `COM7` y `COM8` por los puertos reales. Ambas placas deben usar la misma versión del firmware. Elegir el entorno explícitamente evita depender del entorno predeterminado de PlatformIO.

Las pruebas comienzan al encender. Conectarse a `HWTEST-ESP32-1` o `HWTEST-ESP32-2` desde una terminal Bluetooth clásico SPP y enviar los comandos con salto de línea. El resumen automático aparece cada 10 segundos; los p/s se actualizan cada segundo.

## Comandos

| Comando | Qué muestra o hace |
| --- | --- |
| `STATUS` | Resumen de canales y mediciones de pulsos. |
| `DATA` o `DATOS` | Resultados por canal y placa, y comparación de las copias de informes. |
| `TTL1`, `TTL2`, `RS485`, `CAN` | Diagnóstico detallado del canal, comparaciones y capturas TX/RX. |
| `RAW` | Capturas RX de los cuatro canales de ambas placas, incluidos datos inválidos. |
| `RUN`, `STOP` | Reanuda o detiene los envíos de prueba de la placa conectada. |
| `HELP` | Ayuda de comandos. |

`STOP` mantiene la recepción, las respuestas, los informes y la medición de pulsos. Las capturas crudas tienen límites de tamaño; cuando se recortan, el informe lo indica.

## Documentación y código

- [Guía completa del hardware test](hardware-test/README.md): conexiones, interpretación de resultados, pulsos y límites de las capturas.
- [Firmware](hardware-test/src/main.cpp): código compartido por ambas placas.
- [Configuración de PlatformIO](hardware-test/platformio.ini): entornos de placa 1 y placa 2.
