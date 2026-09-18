# RFC: Cola centralizada de mensajes salientes

Estado: Propuesta detallada para implementacion futura

Objetivo: definir un diseno tecnico para migrar del modelo actual (poll + estado compartido) a una arquitectura con cola centralizada, control de prioridad y observabilidad.

## 1. Problema actual

Problemas detectados en el estado presente:

1. `SendTask` concentra lectura, decision, formateo y envio.
2. Productores escriben en estados locales; no hay persistencia por evento.
3. No hay politica global para overflow ni prioridad.
4. No hay metricas completas de drops/retries/latencia.

## 2. Objetivos

1. Desacoplar produccion de datos del envio fisico.
2. Definir SLA por tipo de mensaje (frescura, prioridad, retry).
3. Hacer observable la salud del pipeline.
4. Habilitar migracion por fases sin corte funcional.

## 3. No objetivos

1. Reescribir todos los parsers CAN en la primera fase.
2. Cambiar protocolo STX existente en la primera fase.
3. Introducir almacenamiento persistente en flash para cola (fase posterior).

## 4. Diseño propuesto

## 4.1 Envelope de mensaje

Estructura conceptual:

```c
typedef enum {
  MSG_WEATHER,
  MSG_PRESSURE,
  MSG_PULSE,
  MSG_CAN_J1939,
  MSG_CAN_ISOBUS_1,
  MSG_CAN_ISOBUS_2,
  MSG_VG55R,
  MSG_BEACON,
  MSG_SYSTEM
} MessageType;

typedef enum {
  PRIO_HIGH,
  PRIO_NORMAL,
  PRIO_LOW
} MessagePriority;

typedef struct {
  MessageType type;
  MessagePriority priority;
  uint32_t produced_at_ms;
  uint32_t seq;
  uint16_t len;
  uint8_t retry_count;
  char payload[256];
} MessageEnvelope;
```

## 4.2 Topologia de colas

Opcion recomendada para MVP:
- Cola HIGH (capacidad menor, latencia baja)
- Cola NORMAL (capacidad principal)
- Cola LOW (mensajes menos criticos)

Dispatcher:
- intenta HIGH -> NORMAL -> LOW en cada iteracion
- aplica politicas de retry y frescura antes de enviar

## 4.3 Interfaz minima

Productores:
- `messageQueuePush(type, priority, payload, len, seq, ts, timeout)`
- variante ISR-safe para productores por interrupcion

Consumidor:
- `messageQueuePop(MessageEnvelope* out, TickType_t timeout)`
- `messageQueueAck/Fail` para estadisticas y retry

## 5. Politicas requeridas

## 5.1 Orden

Decision recomendada:
- FIFO dentro de cada prioridad
- prioridad global: HIGH > NORMAL > LOW

Justificacion:
- evita starvation de mensajes criticos
- mantiene orden local estable por criticidad

## 5.2 Backpressure y overflow

Decision recomendada por prioridad:
- HIGH: bloquear breve (`timeout corto`) antes de drop
- NORMAL: drop oldest
- LOW: drop newest

Siempre registrar metricas de drop por tipo/prioridad.

## 5.3 Retry

Decision recomendada:
- max_retries = 3
- backoff exponencial: 100 ms, 300 ms, 1000 ms
- al agotar reintentos: marcar drop definitivo con razon

## 5.4 Freshness

Decision recomendada por tipo (valores iniciales):
- WEATHER: 10000 ms
- PRESSURE: 5000 ms
- PULSE: 3000 ms
- CAN_J1939: 5000 ms
- CAN_ISOBUS: 10000 ms
- VG55R: 5000 ms
- BEACON: 60000 ms

Si un mensaje vence frescura antes de envio, descartar y contabilizar `drop_stale`.

## 5.5 Mensajes compuestos

Caso `STX06 + STX15` (weather):
- producir un mensaje compuesto logico
- expandir en dispatcher en dos envios atomicos de sesion
- si falla uno, aplicar politica de retry de grupo

## 6. Observabilidad

Metricas minimas:
- queue_depth_{high,normal,low}
- enqueue_ok_total
- drop_total (por razon: full, stale, retry_exhausted)
- send_ok_total
- send_fail_total
- retry_total
- latency_ms_p50/p95/p99

Salida recomendada:
- STX diagnostico periodico cada 5 min
- opcion de log detallado solo en modo debug

## 7. Compatibilidad y migracion

## Fase 0: Baseline
- medir comportamiento actual sin cambios funcionales
- agregar contadores de envio/drop en `SendTask`

## Fase 1: Infra cola
- habilitar API de cola en paralelo al modelo actual
- migrar un productor de bajo riesgo (ej. Pressure)

## Fase 2: Migracion progresiva
- mover Weather y Pulse
- luego J1939/ISOBUS/VG55R

## Fase 3: Consolidacion
- remover path legacy de polling por estado compartido
- dejar `SendTask` como dispatcher de queue

## 8. Criterios de aceptacion

1. Ninguna regresion funcional de mensajes STX actuales.
2. Drop rate medible y menor en escenarios de carga.
3. Latencia p95 dentro de objetivo por tipo.
4. Rollback simple via flag de compilacion.

## 9. Riesgos y mitigaciones

1. Saturacion por CAN burst
- mitigar con prioridad y politica stale/drop

2. Starvation de baja prioridad
- mitigar con cuota minima por ciclo para LOW

3. Fragmentacion de memoria
- usar buffers fijos y estructuras estaticas

4. Deadlocks por bloqueos cruzados
- prohibir locks de productor dentro del dispatcher

## 10. Decisiones iniciales recomendadas (MVP)

1. Tres colas por prioridad.
2. Retry exponencial 3 intentos.
3. Freshness por tipo con valores iniciales de seccion 5.4.
4. Telemetria obligatoria de drops/retries.
5. Migracion por productor, sin big-bang.

## 11. Relacion con implementacion actual

- Reusar `Messanger_queue` como base solo si cumple politicas de prioridad y metricas.
- Si no alcanza, crear modulo nuevo (ej. `MessageQueue`) y mantener wrappers de compatibilidad temporal.
- `builderQueue`/`senderQueue` actuales pueden quedar como puente durante fase de transicion.
