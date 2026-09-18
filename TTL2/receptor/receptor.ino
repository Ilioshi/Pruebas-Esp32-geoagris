#include <Arduino.h>
#include <BluetoothSerial.h>

// Cambiar solo el rol entre placas. TTL2 comparte UART0 con el USB.
// Tras cargar: desconectar USB y alimentar por la entrada habitual de la placa.
// Cruzar TX2 con RX2 y unir GND. No conectar TRAX durante esta prueba.
const bool ES_GENERADOR = false;
const char* NOMBRE_BT = "TTL2_RECEPTOR";
HardwareSerial& TTL2 = Serial;
BluetoothSerial BT;
const int RX_TTL2 = 3, TX_TTL2 = 1;
const uint32_t ESPERA_MS = 1500;
bool automatico = ES_GENERADOR, pendiente = false;
uint32_t numero = 0, correctas = 0, fallidas = 0, recibidas = 0;
uint32_t inicioPrueba = 0, ultimaPrueba = 0;
bool huboDatos = false;
String esperada;

// Recibir una linea completa, sin esperar ni acumular datos sin limite.
// La app debe enviar fin de linea LF o CRLF.
struct Linea {
  String texto;
  bool larga = false;
  uint32_t ultimoByte = 0;
  bool leer(Stream& puerto, String& salida, uint32_t plazo) {
    if ((texto.length() || larga) && millis() - ultimoByte >= plazo) {
      texto = ""; larga = false;
    }
    for (int i = 0; i < 64 && puerto.available(); ++i) {
      char c = (char)puerto.read();
      ultimoByte = millis();
      if (c == '\r' || c == '\n') {
        if (!texto.length() && !larga) continue; // CRLF: no segunda linea vacia.
        salida = larga ? "ERROR: linea demasiado larga" : texto;
        texto = ""; larga = false;
        return true;
      }
      if (texto.length() < 160) texto += c;
      else larga = true;
    }
    return false;
  }
} entradaBT, entradaTTL2;

void informar(const String& texto) {
  if (BT.hasClient()) BT.println(texto); // Nunca imprimir logs por Serial/UART0.
}

String rolLocal() { return ES_GENERADOR ? "G" : "R"; }
String rolRemoto() { return ES_GENERADOR ? "R" : "G"; }

void ayuda() {
  informar(String("PRUEBA TTL2 - ") + NOMBRE_BT);
  informar("UART0: RX3 / TX1 / 115200 8N1. Solo TTL2, no TTL1.");
  informar("BT hola: respuesta Bluetooth local, SIN probar TTL2.");
  informar("TTL2 hola: enviar hola a la otra placa y comprobar retorno.");
  informar("Tambien podes escribir texto directamente para probar TTL2.");
  informar("AUTO: pruebas cada 3 segundos. PARAR: detener automaticas.");
  informar("ESTADO: contadores. AYUDA: instrucciones. Usar fin de linea LF.");
}

void estado() {
  informar("TTL2 ida y vuelta: correctas=" + String(correctas) +
           " fallidas=" + String(fallidas));
  informar("Pedidos recibidos de la otra placa=" + String(recibidas) +
           " | Automatico=" + String(automatico ? "SI" : "NO"));
}

void iniciarPrueba(const String& texto) {
  if (pendiente) {
    informar("Hay una prueba en curso. Espera el resultado y volve a enviar.");
    return;
  }
  if (!texto.length() || texto.length() > 64) {
    informar("Usar entre 1 y 64 bytes de texto por prueba.");
    return;
  }
  ++numero;
  String detalle = String(numero) + " " + texto;
  String pedido = "PRUEBA " + rolLocal() + " " + detalle;
  esperada = "RESPUESTA " + rolRemoto() + " " + detalle;
  pendiente = true;
  huboDatos = false;
  inicioPrueba = ultimaPrueba = millis();
  TTL2.println(pedido);
  informar("Enviando por TTL2: " + pedido);
}

void comandoBT(String texto) {
  if (texto == "AYUDA") { ayuda(); return; }
  if (texto == "ESTADO") { estado(); return; }
  if (texto == "PARAR") {
    automatico = false;
    informar("Pruebas automaticas detenidas; la prueba en curso puede terminar.");
    return;
  }
  if (texto == "AUTO") {
    automatico = true;
    informar("Pruebas automaticas habilitadas.");
    return;
  }
  if (texto == "BT" || texto.startsWith("BT ")) {
    informar("BT OK: recibido y respondido localmente: " + texto);
    informar("Este resultado NO comprueba TTL2.");
    return;
  }
  // Un pedido manual pausa las pruebas automaticas para no mezclar resultados.
  automatico = false;
  if (texto.startsWith("TTL2 ")) texto.remove(0, 5);
  iniciarPrueba(texto);
}

// Valida el numero inicial; el resto del texto se devuelve sin modificar.
bool detalleValido(const String& detalle) {
  int espacio = detalle.indexOf(' ');
  if (espacio < 1 || espacio > 10 || detalle.length() <= (size_t)espacio + 1)
    return false;
  for (int i = 0; i < espacio; ++i)
    if (detalle[i] < '0' || detalle[i] > '9') return false;
  return true;
}

void mensajeTTL2(const String& texto) {
  String prefijo = "PRUEBA " + rolRemoto() + " ";
  if (texto.startsWith(prefijo)) {
    String detalle = texto.substring(prefijo.length());
    if (!detalleValido(detalle)) {
      informar("TTL2: pedido recibido con formato incorrecto.");
      return;
    }
    ++recibidas;
    String respuesta = "RESPUESTA " + rolLocal() + " " + detalle;
    size_t escritos = TTL2.println(respuesta);
    informar("Recepcion TTL2 correcta: " + texto);
    informar(escritos == respuesta.length() + 2 ?
             "Respuesta entregada al UART: " + respuesta :
             "FALLA: el UART no acepto la respuesta completa.");
    // Esto NO acredita llegada. Solo la placa que inicio puede confirmar retorno.
    return;
  }
  if (pendiente && texto == esperada) {
    pendiente = false;
    ++correctas;
    informar("Recibida por TTL2: " + texto);
    informar("OK: TTL2 ida y vuelta confirmada.");
    estado();
  } else {
    informar("TTL2: dato no esperado, NO cuenta como prueba correcta: " + texto);
  }
}

void setup() {
  TTL2.begin(115200, SERIAL_8N1, RX_TTL2, TX_TTL2);
  TTL2.setDebugOutput(false);
  BT.begin(NOMBRE_BT);
  ultimaPrueba = millis();
}

void loop() {
  static bool conectadoAntes = false;
  bool conectado = BT.hasClient();
  if (conectado && !conectadoAntes) ayuda(); // Visible aunque conectes tarde.
  if (!conectado && conectadoAntes) {
    entradaBT.texto = ""; entradaBT.larga = false;
  }
  conectadoAntes = conectado;

  String texto;
  if (entradaBT.leer(BT, texto, 10000)) {
    if (texto == "ERROR: linea demasiado larga") informar(texto);
    else comandoBT(texto);
  }

  // Detectar datos, incluso si no forman una linea completa.
  int disponibles = TTL2.available();
  if (pendiente && disponibles > 0) huboDatos = true;
  if (entradaTTL2.leer(TTL2, texto, 1000)) mensajeTTL2(texto);

  if (pendiente && millis() - inicioPrueba >= ESPERA_MS) {
    pendiente = false;
    ++fallidas;
    informar(!huboDatos ?
             "FALLA TTL2: no llego ningun dato de retorno en 1,5 segundos." :
             "FALLA TTL2: llegaron datos, pero no la respuesta completa y correcta.");
    estado();
  }
  if (automatico && !pendiente && millis() - ultimaPrueba >= 3000)
    iniciarPrueba("automatico");
  delay(1);
}
