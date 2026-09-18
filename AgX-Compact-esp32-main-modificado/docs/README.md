# Centro de documentacion

Este directorio concentra la documentacion tecnica viva del firmware AGX Compact ESP32.

Objetivo:
- entender arquitectura real y responsabilidades por modulo,
- compilar y desplegar de forma reproducible,
- seguir el flujo actual de mensajes,
- preparar la evolucion hacia una cola de mensajeria robusta.

## README unificado

- El README principal del repo se genera automaticamente desde modulos en `docs/`.
- Fuente de verdad: archivos `docs/*.md` (no editar secciones tecnicas manualmente en `README.md`).
- Regeneracion:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_readme.ps1
```

## Orden de lectura recomendado

1. [01_architecture.md](01_architecture.md)
2. [07_modules_explained.md](07_modules_explained.md)
3. [02_build_deploy_test.md](02_build_deploy_test.md)
4. [03_protocols_and_messages.md](03_protocols_and_messages.md)
5. [04_hardware_and_flags.md](04_hardware_and_flags.md)
6. [05_current_message_flow.md](05_current_message_flow.md)
7. [06_rfc_message_queue.md](06_rfc_message_queue.md)
8. [TODO.md](TODO.md)

## Por necesidad

- Si queres compilar, subir firmware o monitorear:
  - [02_build_deploy_test.md](02_build_deploy_test.md)
- Si queres entender arquitectura y como se relacionan tareas:
  - [01_architecture.md](01_architecture.md)
  - [07_modules_explained.md](07_modules_explained.md)
- Si queres ver formatos y protocolos:
  - [03_protocols_and_messages.md](03_protocols_and_messages.md)
- Si queres revisar hardware, pines y flags:
  - [04_hardware_and_flags.md](04_hardware_and_flags.md)
- Si queres trabajar sobre flujo actual de salida:
  - [05_current_message_flow.md](05_current_message_flow.md)
- Si queres diseñar la cola futura:
  - [06_rfc_message_queue.md](06_rfc_message_queue.md)
- Si queres priorizar pendientes:
  - [TODO.md](TODO.md)

## Reglas de mantenimiento de docs

1. Todo cambio de codigo debe actualizar su documento tecnico afectado.
2. Mantener consistencia entre nombres de flags, tareas y entornos.
3. Separar siempre estado actual de propuestas futuras.
4. Resolver y cerrar TODOs en docs cuando se resuelvan en codigo.
5. Actualizar primero docs y luego regenerar `README.md` con `scripts/build_readme.ps1`.

## Herramientas y referencias externas

- PlatformIO oficial: https://platformio.org/
- Analisis estatico en PlatformIO: https://docs.platformio.org/en/latest/advanced/static-code-analysis/index.html

Nota de estado:
- El analisis estatico todavia no esta integrado en este proyecto.
- Queda recomendado como mejora futura para detectar errores potenciales y puntos de falla.

## Equipo y contribuyentes

[![Avatar de @omsmarian](https://github.com/omsmarian.png?size=120)](https://github.com/omsmarian)\
[@omsmarian](https://github.com/omsmarian)
