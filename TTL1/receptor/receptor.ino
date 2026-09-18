#include <Arduino.h>

// Cargar en la placa que se quiere comprobar.
// Conectar TX1 a RX1, RX1 a TX1 y GND comun. Sin TRAX.
// Monitor USB y TTL1: 115200 baudios, 8N1.
HardwareSerial TTL1(2);
const int RX_TTL1 = 25;
const int TX_TTL1 = 26;
String linea;
bool demasiadoLarga = false;
uint32_t ultimoByte = 0;

// Aceptar solamente PRUEBA seguida de un numero (hasta 10 digitos).
bool pedidoValido(const String& texto) {
  if (!texto.startsWith("PRUEBA ") || texto.length() < 8 || texto.length() > 17)
    return false;
  for (size_t i = 7; i < texto.length(); ++i)
    if (texto[i] < '0' || texto[i] > '9') return false;
  return true;
}

void responder() {
  if (demasiadoLarga || !pedidoValido(linea)) {
    Serial.println("Dato recibido por TTL1, pero el mensaje no es valido.");
    return;
  }
  String respuesta = "RESPUESTA " + linea.substring(7);
  Serial.println("Recepcion TTL1 correcta: " + linea);

  // Entregar al UART no garantiza que la senal llegue a la otra placa.
  size_t enviados = TTL1.println(respuesta);
  if (enviados == respuesta.length() + 2) {
    Serial.println("Respuesta entregada al UART: " + respuesta);
    Serial.println("La llegada debe confirmarse en la placa generadora.\n");
  } else {
    Serial.println("FALLA: el UART no acepto la respuesta completa.\n");
  }
}

void setup() {
  Serial.begin(115200);
  TTL1.begin(115200, SERIAL_8N1, RX_TTL1, TX_TTL1);
  delay(1000);
  Serial.println("\nPRUEBA TTL1 - PLACA RECEPTORA");
  Serial.println("Esperando mensajes PRUEBA por TTL1.");
}

void loop() {
  if ((linea.length() || demasiadoLarga) && millis() - ultimoByte >= 1000) {
    Serial.println("Dato recibido por TTL1, pero el mensaje quedo incompleto.");
    linea = "";
    demasiadoLarga = false;
  }
  // Leer por partes, sin bloquear el resto del programa.
  for (int i = 0; i < 64 && TTL1.available(); ++i) {
    char c = (char)TTL1.read();
    ultimoByte = millis();
    if (c == '\r') continue;
    if (c == '\n') {
      responder();
      linea = "";
      demasiadoLarga = false;
    } else if (linea.length() < 40) {
      linea += c;
    } else {
      demasiadoLarga = true;
    }
  }
  delay(1);
}
