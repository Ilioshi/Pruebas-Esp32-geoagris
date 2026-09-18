#include <Arduino.h>

// Cargar en la placa de prueba que sabemos que funciona.
// Conectar TX1 a RX1, RX1 a TX1 y GND comun. Sin TRAX.
// Monitor USB y TTL1: 115200 baudios, 8N1.
HardwareSerial TTL1(2);
const int RX_TTL1 = 25;
const int TX_TTL1 = 26;
uint32_t prueba = 0, correctas = 0, fallidas = 0;

void setup() {
  Serial.begin(115200);
  TTL1.begin(115200, SERIAL_8N1, RX_TTL1, TX_TTL1);
  delay(1000);
  Serial.println("\nPRUEBA TTL1 - PLACA GENERADORA");
  Serial.println("Espera una RESPUESTA con el mismo numero de PRUEBA.");
}

void loop() {
  ++prueba;
  String pedido = "PRUEBA " + String(prueba);
  String esperada = "RESPUESTA " + String(prueba);

  // Limpiar datos de la prueba anterior, sin esperar indefinidamente.
  for (int i = 0; i < 256 && TTL1.available(); ++i) TTL1.read();

  TTL1.println(pedido); // El salto de linea indica el fin del mensaje.
  Serial.println("Enviando: " + pedido);

  String linea;
  uint32_t bytesRecibidos = 0;
  bool correcta = false, demasiadoLarga = false;
  const uint32_t inicio = millis();

  // Esperar hasta un segundo. Limitar longitud para tolerar ruido.
  while (millis() - inicio < 1000 && !correcta) {
    if (!TTL1.available()) { delay(1); continue; }
    char c = (char)TTL1.read();
    ++bytesRecibidos;
    if (c == '\r') continue;
    if (c == '\n') {
      correcta = !demasiadoLarga && linea == esperada;
      linea = "";
      demasiadoLarga = false;
    } else if (linea.length() < 40) {
      linea += c;
    } else {
      demasiadoLarga = true;
    }
  }

  if (correcta) {
    ++correctas;
    Serial.println("Recibida: " + esperada);
    Serial.println("OK: comunicacion TTL1 de ida y vuelta.");
  } else {
    ++fallidas;
    if (bytesRecibidos == 0)
      Serial.println("FALLA: no llego ningun dato de retorno por TTL1 en 1 segundo.");
    else
      Serial.println("FALLA: llegaron datos, pero no la respuesta completa y correcta.");
  }
  Serial.printf("Pruebas correctas: %lu | Pruebas fallidas: %lu\n\n",
                (unsigned long)correctas, (unsigned long)fallidas);
  delay(1000);
}
