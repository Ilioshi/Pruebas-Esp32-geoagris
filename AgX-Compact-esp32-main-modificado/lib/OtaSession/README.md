# OtaSession lib

Esta librería expone la clase **OtaSession** que abstrae la lógica de interpretación de paquetes para OTA y flasheado de la nueva imagen de firmware.

## Iniciar una sesión OTA
Para iniciar una sesión OTA, es necesario crear una instancia del objeto mediante el método fábrica *OtaSession::begin*.
```
 static OtaSession* begin(BleSerial* _ble)
```

- `BleSerial* _ble`: una referencia al objeto ble serial (*G_ble*).

Donde vamos a recibir un puntero a nuestra sesión de OTA (alocado en *heap*). En caso de **error** va a devolver **nullptr**, es necesario chequear si la creación fue exitosa (*session != nullptr*).

```
OtaSession* otaSession = OtaSession::begin(&G_ble);
if (otaSession != nullptr) {
    /* Enviar handshake */
}
```

## Recibir paquetes
Una vez creada nuestra sesión, podemos pasarle directamente el buffer de lectura del BLE al método *handlePacket*.
```
void handlePacket(uint8_t* data, int len);
```
- `uint8_t data`: puntero al buffer de data (*datable*).
- `int len`: longitud del buffer.

Este método se va a encargar de parsear el buffer, leer los paquetes, escribirlos y responderle al cliente. En caso de recibir datos corruptos y/o desordenados se va a ocupar de hacerselo saber al cliente (ver "Protocolo OTA")

```
len = G_ble.readBytes(datable, sizeof(datable));

if (len > 0)
{
    lastBleDataReceivedTime = millis();

    if (otaSession != nullptr)
        otaSession->handlePacket(datable, len);
// ...
```

## Finalizar la transferencia
Si la transferencia fue exitosa, la sesión va a reiniciar automáticamente el ESP32 y lo va a bootear con la nueva imagen flasheada.

## ¿Qué pasa si falla?
Si falla, va a abortar la escritura y va a levantar una flag que se puede consultar con el método *isActive*.

```bool isActive()```

**Es necesario consultar esta flag para levantar la sesión cuando haya fallado.**

## Cerrar la sesión
En caso de que se haya perdido la conexión (responsabilidad de la implementación de la librería, no tiene timeout la librería) o que por alguna razón se desee abortar y cerrar la sesión, se hace de la siguiente manera:

```
// Le decimos a ESP-IDF que va a dejar de escribir la memoria
otaSession->abort();

// Liberamos la memoria que ocupa otaSession (porque está alocado en heap)
delete otaSession;

// Seteamos nuestro puntero a nullptr
otaSession = nullptr;
```

El paso `otaSession = nullptr` es necesario ya que con delete solamente estamos liberando la memoria, pero el puntero sigue apuntand a esa dirección, por lo que cuando hagamos `if (otaSession != nullptr)` vamos a tener comportamiento indefinido.

## Protocolo OTA
Todo en ASCII, el packet ID va con padding a la izquierda. Por ejemplo: \>OTADATA00002__________<

La data tiene una longitud fija de 480 bytes. Esto está definido en `OTA_PACKET_SIZE` en **otasession.h**. Elegí 480 bytes porque el BLE del ESP32 tiene un MTU de ~512 bytes. La data va *con los bytes crudos*, no usa byte64 ni ningún otro encoding.

- **Recomendación:** Poner un timeout de ~1 seg entre cada envío de paquete, que sea interrumpible por cualquier paquete enviado del ESP32 al cliente. A veces se superponen los paquetes y la velocidad de transferencia cae en un 50%.

### Paquetes del cliente al ESP32
```
>OTADATA,P,L,______________________<
P is packet ID. (No padding needed)
L is data length.
_____ is data.

>OTAFINISH<
No more packets left.
```
### Paquetes del ESP32 al cliente.
```
>OTAOKP<
Packet P succesfully written.

>OTACURRENTP<
P is current packet ID.
Used to sync packet ID with client.

>OTAFAILEDP<
Failed to write P packet ID.

```
Cuando el cliente recibe OTACURRENT puede asumir OTAFAILED para simplificar la lógica.

#### Paquetes enviados al finalizar la transferencia (después de recibir >OTAFINISH<)
```
>OTAFINISHP<
OTA succesfull with last packet ID P

>OTACORRUPTEDP<
Received data was corrupted.
P is last packet ID.

>OTAFAILEDP<
OTA failed (other error, not corrupted)
P is last packet ID.
```
