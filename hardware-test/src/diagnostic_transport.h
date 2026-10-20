// Transporte de diagnostico v2. Incluir despues de los receptores de mensajes.
// Placa 1 arbitra los intercambios: tambien sirve para RS485 half duplex.
#pragma once

static constexpr size_t AUX_MAX_PACKET = 512;
static constexpr uint32_t AUX_TIMEOUT_MS = 2000;
static constexpr uint32_t AUX_HEALTH_MS = 20000;
static constexpr uint32_t AUX_CAPTURE_MS = 120000;
static constexpr uint16_t AUX_CAN_ID = 0x520;

struct AuxFlight {
  uint32_t sequence = 0;
  String message;
};
struct AuxCanPacket {
  String text;
  uint16_t length = 0;
  uint8_t token = 0;
  uint8_t index = 0;
  bool active = false;
  uint32_t nextAt = 0;
};
struct AuxHealth {
  uint32_t lastReply = 0;
  uint32_t replies = 0;
  uint32_t timeouts = 0;
  uint8_t consecutiveFailures = 0;
};

std::deque<String> diagnosticDetailQueue;
AuxFlight auxFlight;
AuxCanPacket auxCanTx, auxCanRx;
AuxHealth auxHealth[CHANNEL_COUNT];
uint32_t auxNextSequence = 0;
uint32_t auxSession = 1;
uint32_t auxPeerSession = 0;
uint32_t auxLastReceived = 0;
uint32_t auxPollNonce = 0;
uint32_t auxWaitingNonce = 0;
uint32_t auxDeadline = 0;
uint32_t auxNextPoll = 0;
uint32_t auxNextProbe = 0;
uint32_t auxRemoteLastPoll = 0;
uint32_t auxRequestCounter = 0;
uint32_t auxRequestId = 0;
uint32_t auxRequestAt = 0;
uint32_t auxServedRequest = 0;
uint8_t auxProbe = 0;
int8_t auxRoute = -1;
int8_t auxWaitingVia = -1;
uint8_t auxRequestedMask = 0;
bool auxRequestedRaw = false;
bool auxDataPoll = false;
uint8_t auxCanToken = 0;
bool auxRs485Transmitting = false;
String auxRs485Response;
uint32_t auxRs485ResponseDue = 0;

void printDetails();
void printData();
void printChannelDetails(uint8_t channel);
void queueDetailedDiagnostics(uint8_t mask);

bool auxTimeReached(uint32_t now, uint32_t due) {
  return static_cast<int32_t>(now - due) >= 0;
}

String auxChecked(const String& body) {
  char crc[6];
  snprintf(crc, sizeof(crc), "*%04X", diagnosticCrc(body));
  return body + crc;
}

void auxLoadFlight() {
  if (auxFlight.sequence) return;
  std::deque<String>& queue = !diagnosticQueue.empty() ? diagnosticQueue :
                                                            diagnosticDetailQueue;
  if (queue.empty()) return;
  auxFlight.message = queue.front();
  queue.pop_front();
  if (++auxNextSequence == 0) ++auxNextSequence;
  auxFlight.sequence = auxNextSequence;
}

void auxAcknowledge(uint32_t ack) {
  if (auxFlight.sequence && ack == auxFlight.sequence) auxFlight = AuxFlight{};
}

void auxDeliver(String message) {
  if (!verifyDiagnostic(message)) { ++diagnosticRejected; return; }
  if (message.startsWith("DETAIL,")) {
    unsigned long sender = 0, request = 0, mask = 0;
    char extra;
    if (sscanf(message.c_str(), "DETAIL,%lu,%lu,%lu%c", &sender, &request,
               &mask, &extra) == 3 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
        request && mask > 0 && mask <= 15) {
      if (!localReportId) {
        sendDiagnostic("WAIT," + String(DEVICE_ID) + "," + String(request), true);
        return;
      }
      if (auxServedRequest == request) return;
      auxServedRequest = request;
      // Una nueva solicitud reemplaza solo lo que todavia no se transmitio.
      diagnosticDetailQueue.clear();
      queueDetailedDiagnostics(static_cast<uint8_t>(mask));
      diagnosticDetailQueue.push_back(auxChecked("DONE," + String(DEVICE_ID) +
        "," + String(request) + "," + String(mask) + "," + String(localReportId)));
    } else {
      ++diagnosticRejected;
    }
    return;
  }
  if (message.startsWith("WAIT,")) {
    unsigned long sender = 0, request = 0;
    char extra;
    if (sscanf(message.c_str(), "WAIT,%lu,%lu%c", &sender, &request, &extra) == 2 &&
        sender == (DEVICE_ID == 1 ? 2UL : 1UL) && auxRequestId == request) {
      auxRequestId = 0;
      report("La otra placa aun no cerro una ventana. Esperar 10 s y repetir comando.");
    }
    return;
  }
  if (message.startsWith("DONE,")) {
    unsigned long sender = 0, request = 0, mask = 0, id = 0;
    char extra;
    if (sscanf(message.c_str(), "DONE,%lu,%lu,%lu,%lu%c", &sender, &request,
               &mask, &id, &extra) == 4 && sender == (DEVICE_ID == 1 ? 2UL : 1UL) &&
        auxRequestId && request == auxRequestId && mask == auxRequestedMask &&
        id > 0 && id <= 255) {
      auxRequestId = 0;
      report("Detalle remoto recibido; informe " + String(id) +
             ". Las ventanas locales y remotas pueden ser distintas.");
      if (auxRequestedRaw) printData();
      else for (uint8_t ch = 0; ch < CHANNEL_COUNT; ++ch)
        if (mask & (1 << ch)) printChannelDetails(ch);
    }
    return;
  }
  if (receiveComparison(message) || receiveFlow(message) || receivePulse(message) ||
      receiveRawTrace(message) || receiveReceipt(message) || receiveRs485Trace(message))
    return;
  ++diagnosticRejected;
}

bool auxTransmit(uint8_t via, const String& body) {
  const String packet = auxChecked(body);
  if (packet.length() > AUX_MAX_PACKET) { ++diagnosticDropped; return false; }
  if (via == 3) {
    if (auxCanTx.active) return false;
    auxCanTx = AuxCanPacket{};
    auxCanTx.text = packet;
    auxCanTx.length = packet.length();
    auxCanTx.token = ++auxCanToken;
    auxCanTx.index = 0xFF;
    auxCanTx.active = true;
    auxCanTx.nextAt = millis();
    return true;
  }
  HardwareSerial& port = via == 0 ? ttl1 : via == 1 ? ttl2 : rs485;
  if ((via == 2 && auxRs485Transmitting) ||
      port.availableForWrite() < static_cast<int>(packet.length() + 2))
    return false;
  if (via == 2) {
    digitalWrite(RS485_DE_RE, HIGH);
    delayMicroseconds(100);
  }
  port.println(packet);
  if (via == 2) {
    // El buffer TX evita detener el loop hasta medio segundo con una captura.
    auxRs485Transmitting = true;
  }
  // No incluir AUX en capturas: evita encapsular capturas dentro de si mismas.
  return true;
}

void auxPumpCan() {
  if (!auxCanTx.active || !auxTimeReached(millis(), auxCanTx.nextAt)) return;
  struct can_frame frame{};
  frame.can_id = AUX_CAN_ID + DEVICE_ID;
  frame.can_dlc = 8;
  frame.data[0] = auxCanTx.token;
  frame.data[1] = auxCanTx.index;
  if (auxCanTx.index == 0xFF) {
    frame.data[2] = auxCanTx.length & 0xFF;
    frame.data[3] = auxCanTx.length >> 8;
    frame.data[4] = 2;  // Version del transporte.
  } else {
    const size_t offset = static_cast<size_t>(auxCanTx.index) * 6;
    for (uint8_t i = 0; i < 6 && offset + i < auxCanTx.length; ++i)
      frame.data[2 + i] = auxCanTx.text[offset + i];
  }
  if (canController.sendMessage(&frame) == MCP2515::ERROR_OK) {
    if (auxCanTx.index == 0xFF) auxCanTx.index = 0;
    else if (static_cast<size_t>(++auxCanTx.index) * 6 >= auxCanTx.length)
      auxCanTx.active = false;
  }
  auxCanTx.nextAt = millis() + 8;
}

String auxPacketBody(uint32_t nonce, bool data) {
  if (data) auxLoadFlight();
  return "DX," + String(DEVICE_ID) + "," + String(auxSession) + "," + String(nonce) + "," +
    String(data ? auxFlight.sequence : 0) + "," + String(auxLastReceived) + "," +
    (data ? "D," : "P,") + (data && auxFlight.sequence ? auxFlight.message : "-");
}

bool auxReceivePacket(String packet, uint8_t via) {
  if (!packet.startsWith("DX,")) return false;
  if (!verifyDiagnostic(packet)) { ++diagnosticRejected; return true; }
  unsigned long sender = 0, session = 0, nonce = 0, sequence = 0, ack = 0;
  char mode = 0;
  int used = 0;
  if (sscanf(packet.c_str(), "DX,%lu,%lu,%lu,%lu,%lu,%c,%n", &sender, &session, &nonce,
             &sequence, &ack, &mode, &used) != 6 || !used || !nonce || !session ||
      sender != (DEVICE_ID == 1 ? 2UL : 1UL) || (mode != 'D' && mode != 'P')) {
    ++diagnosticRejected;
    return true;
  }
  const String payload = packet.substring(used);
  if ((mode == 'P' && (sequence || payload != "-")) ||
      (!sequence && payload != "-")) { ++diagnosticRejected; return true; }
  if (sequence) {
    String checked = payload;
    if (!verifyDiagnostic(checked)) { ++diagnosticRejected; return true; }
  }
  if (DEVICE_ID == 1) {
    if (auxWaitingVia != via || nonce != auxWaitingNonce ||
        (mode == 'D') != auxDataPoll) return true;
    auxWaitingVia = -1;
    auxHealth[via].lastReply = millis();
    ++auxHealth[via].replies;
    auxHealth[via].consecutiveFailures = 0;
    auxNextPoll = millis() + 25;
    if (via == 2) auxRs485QuietUntil = millis() + 10;
  } else {
    // El maestro conserva el control del bus y decide la via de cada intercambio.
    auxRemoteLastPoll = millis();
    if (mode == 'D') auxRoute = via;
  }
  if (auxPeerSession != session) {
    const bool restarted = auxPeerSession != 0;
    auxPeerSession = session;
    auxLastReceived = 0;
    auxServedRequest = 0;
    peerPulseReceivedAt = 0;
    latestPeerReportId = 0;
    latestPeerAt = 0;
    for (uint8_t ch = 0; ch < CHANNEL_COUNT; ++ch) {
      pendingRxTrace[ch] = PeerRawTrace{};
      pendingTxTrace[ch] = PeerRawTrace{};
      peerRxTrace[ch] = PeerRawTrace{};
      peerTxTrace[ch] = PeerRawTrace{};
      peerComparison[ch][0] = PeerFrameComparison{};
      peerFlow[ch] = FlowCopy{};
      peerReceipts[ch] = ReportReceipt{};
      for (uint8_t via = 0; via < CHANNEL_COUNT; ++via)
        peerCopies[via][ch] = PeerCopy{};
    }
    if (restarted && auxRequestId)
      sendDiagnostic("DETAIL," + String(DEVICE_ID) + "," + String(auxRequestId) +
                     "," + String(auxRequestedMask), true);
  }
  auxAcknowledge(ack);
  const bool fresh = sequence && (auxLastReceived == 0 ||
                            static_cast<int32_t>(sequence - auxLastReceived) > 0);
  if (fresh) auxLastReceived = sequence;
  // Confirmar antes de imprimir o generar capturas, para no bloquear el intercambio.
  if (DEVICE_ID == 2) {
    if (via == 2) {
      auxRs485Response = auxPacketBody(nonce, mode == 'D');
      auxRs485ResponseDue = millis() + 10;  // Permitir liberar DE al maestro.
    } else auxTransmit(via, auxPacketBody(nonce, mode == 'D'));
  }
  if (fresh) auxDeliver(payload);
  return true;
}

bool auxReceiveCan(const struct can_frame& frame) {
  if (frame.can_id != AUX_CAN_ID + (DEVICE_ID == 1 ? 2 : 1)) return false;
  if (frame.can_dlc != 8) { ++diagnosticRejected; return true; }
  const uint8_t token = frame.data[0], index = frame.data[1];
  if (index == 0xFF) {
    auxCanRx = AuxCanPacket{};
    const uint16_t length = frame.data[2] | (frame.data[3] << 8);
    if (frame.data[4] != 2 || !length || length > AUX_MAX_PACKET) {
      ++diagnosticRejected;
      return true;
    }
    auxCanRx.active = true;
    auxCanRx.length = length;
    auxCanRx.token = token;
    auxCanRx.nextAt = millis() + AUX_TIMEOUT_MS;
    return true;
  }
  if (!auxCanRx.active || token != auxCanRx.token || index != auxCanRx.index ||
      auxTimeReached(millis(), auxCanRx.nextAt)) {
    auxCanRx.active = false;
    ++diagnosticRejected;
    return true;
  }
  for (uint8_t i = 0; i < 6 && auxCanRx.text.length() < auxCanRx.length; ++i)
    auxCanRx.text += static_cast<char>(frame.data[2 + i]);
  ++auxCanRx.index;
  if (auxCanRx.text.length() == auxCanRx.length) {
    auxCanRx.active = false;
    auxReceivePacket(auxCanRx.text, 3);
  }
  return true;
}

bool auxHealthy(uint8_t via) {
  const AuxHealth& health = auxHealth[via];
  return health.replies && millis() - health.lastReply <= AUX_HEALTH_MS &&
         health.consecutiveFailures < 2;
}

void auxSelectRoute() {
  if (auxRoute >= 0 && auxHealthy(auxRoute)) return;
  const int8_t previous = auxRoute;
  auxRoute = -1;
  // UART, CAN y finalmente RS485; conservar la via elegida mientras sea estable.
  const uint8_t priority[] = {0, 1, 3, 2};
  for (uint8_t via : priority) if (auxHealthy(via)) { auxRoute = via; break; }
  if (previous != auxRoute)
    report(auxRoute < 0 ? "Sin via auxiliar confirmada; resultados locales disponibles" :
           "Diagnostico remoto por " + String(CHANNEL_NAMES[auxRoute]));
}

bool auxRs485Busy() {
  return auxWaitingVia == 2 || auxRs485Transmitting || auxRs485Response.length() ||
    !auxTimeReached(millis(), auxRs485QuietUntil) || rs485ReportDue != 0;
}

void processDiagnosticOutput() {
  if (auxRs485Transmitting && uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK) {
    delayMicroseconds(100);
    digitalWrite(RS485_DE_RE, LOW);
    auxRs485Transmitting = false;
  }
  if (auxRs485Response.length() && auxTimeReached(millis(), auxRs485ResponseDue) &&
      auxTransmit(2, auxRs485Response)) auxRs485Response = "";
  auxPumpCan();
  if (auxCanRx.active && auxTimeReached(millis(), auxCanRx.nextAt))
    auxCanRx.active = false;
  if (auxRequestId && millis() - auxRequestAt > AUX_CAPTURE_MS) {
    auxRequestId = 0;
    report("Detalle remoto incompleto: plazo agotado. Repetir comando o consultar la otra placa.");
  }
  if (DEVICE_ID != 1) return;
  const uint32_t now = millis();
  if (auxWaitingVia >= 0) {
    if (!auxTimeReached(now, auxDeadline)) return;
    AuxHealth& health = auxHealth[auxWaitingVia];
    ++health.timeouts;
    if (health.consecutiveFailures < 255) ++health.consecutiveFailures;
    auxWaitingVia = -1;
    auxCanTx.active = false;
    auxNextPoll = now + 25;
  }
  auxSelectRoute();
  if (auxCanTx.active || !auxTimeReached(now, auxNextPoll)) return;
  uint8_t via;
  bool data;
  if (auxRoute < 0 || auxTimeReached(now, auxNextProbe)) {
    via = auxProbe;
    auxProbe = (auxProbe + 1) % CHANNEL_COUNT;
    data = false;
    auxNextProbe = now + (auxRoute < 0 ? 25 : 3000);
  } else { via = auxRoute; data = true; }
  if (via == 2 && (auxRs485Busy() || rs485Stats.pending ||
                   (automaticTestsEnabled && now - lastPingAt >= PING_INTERVAL_MS)))
    return;
  if (++auxPollNonce == 0) ++auxPollNonce;
  if (!auxTransmit(via, auxPacketBody(auxPollNonce, data))) return;
  auxWaitingVia = via;
  auxWaitingNonce = auxPollNonce;
  auxDataPoll = data;
  // Los sondeos vacios no deben demorar los datos por un canal desconectado.
  auxDeadline = millis() + (data ? (via == 3 ? 3000 : 2000) :
                           (via == 3 ? 600 : 350));
}

void reportAuxTransport() {
  report("Diagnostico auxiliar: " +
    (auxRoute < 0 || (DEVICE_ID == 2 && millis() - auxRemoteLastPoll > AUX_HEALTH_MS) ?
     String("sin via confirmada") : String(CHANNEL_NAMES[auxRoute])));
  report("La via auxiliar transporta datos; no es una referencia de exactitud.");
}

void requestRemoteDetails(uint8_t mask, bool raw) {
  if (auxRequestId) {
    report("Ya hay una solicitud de detalle pendiente; repetir al terminar.");
    return;
  }
  auxRequestedMask = mask;
  auxRequestedRaw = raw;
  if (++auxRequestCounter == 0) ++auxRequestCounter;
  auxRequestId = auxRequestCounter;
  auxRequestAt = millis();
  sendDiagnostic("DETAIL," + String(DEVICE_ID) + "," + String(auxRequestId) +
                 "," + String(mask), true);
  report("Solicitando captura remota; se mostrara al completar. CAN/RS485 pueden tardar mas.");
}
