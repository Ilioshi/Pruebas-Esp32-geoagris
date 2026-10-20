#include "Arduino.h"
#include <iostream>
#include <stdexcept>

HostBoard a, b;
uint32_t nextPulse = 0;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void runFor(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (millis() >= nextPulse) { b.addPulse(); nextPulse = millis() + 100; }
    a.loop(); b.loop();
  }
}
void only(int channel) {
  for (int ch = 0; ch < 4; ++ch) links[ch][0] = links[ch][1] = ch == channel;
}
int main(int argc, char** argv) {
  try {
    require(argc == 2, "scenario required");
    std::string scenario = argv[1];
    a = hostBoard1(); b = hostBoard2();
    for (int ch = 0; ch < 3; ++ch) {
      a.ports[ch]->board = 0; b.ports[ch]->board = 1;
      a.ports[ch]->peer = b.ports[ch]; b.ports[ch]->peer = a.ports[ch];
    }
    a.can->board = 0; b.can->board = 1; a.can->peer = b.can; b.can->peer = a.can;
    a.setup(); b.setup(); nextPulse = millis();
    if (scenario == "ttl2_only") only(1);
    else if (scenario == "can_only" || scenario == "can_loss" ||
             scenario == "full_can_raw" || scenario == "mid_raw_failover") only(3);
    else if (scenario == "rs485_only" || scenario == "rs485_nuls") only(2);
    else if (scenario == "all_down") only(-1);
    else if (scenario == "one_way_ttl1") links[0][1] = false;
    else if (scenario == "corrupt_info") serialFilter = [](int via, int, std::string& p) {
      if (via == 1 && p.rfind("INFO,", 0) == 0) p[12] ^= 1;
      return true;
    };
    int dropped = 0;
    if (scenario == "rs485_nuls") serialFilter = [](int via, int, std::string& p) {
      if (via == 2) p.insert(p.begin(), '\0');
      return true;
    };
    if (scenario == "can_loss") canFilter = [&dropped](int, std::vector<uint8_t>& bytes) {
      if (dropped < 3 && bytes[1] == 2) { ++dropped; return false; }
      return true;
    };
    runFor(30000);
    if (scenario == "all_down") {
      require(a.route() == -1, "route claimed with all links disconnected");
      a.command("STATUS"); runFor(1500);
      require(!a.bluetooth->output.empty(), "local status unavailable");
    } else {
      int expected = scenario == "ttl2_only" || scenario == "one_way_ttl1" ? 1 :
        scenario == "can_only" || scenario == "can_loss" ||
        scenario == "full_can_raw" || scenario == "mid_raw_failover" ? 3 :
        scenario == "rs485_only" || scenario == "rs485_nuls" ? 2 : 0;
      require(a.route() == expected, "wrong diagnostic route");
      require(a.pulse() > 0, "remote pulses missing on surviving route");
      require(a.infoMask(expected) == 15, "native summary incomplete on surviving route");
      if (scenario == "corrupt_info") require(a.infoMask(1) == 0, "corrupted INFO accepted");
      if (scenario == "failover") {
        links[0][0] = links[0][1] = false;
        runFor(16000);
        require(a.route() != 0 && a.route() >= 0, "failed TTL1 did not switch route");
        require(a.pulse() > 0, "pulses lost after failover");
      }
      if (scenario == "lost_reply") {
        int losses = 0;
        serialFilter = [&losses](int via, int board, std::string& p) {
          if (via == 0 && board == 1 && p.rfind("DX,", 0) == 0 && losses++ < 2) return false;
          return true;
        };
        a.command("TTL2"); runFor(25000);
        require(a.rawRequest() == 0, "lost replies stranded detail request");
        serialFilter = {};
      }
      if (scenario == "full_can_raw" || scenario == "mid_raw_failover") b.fillCaptures();
      a.command("RAW");
      if (scenario == "mid_raw_failover") {
        runFor(4000);
        links[1][0] = links[1][1] = true;
        links[3][0] = links[3][1] = false;
        runFor(111000);
        require(a.route() == 1, "capture did not recover through TTL2");
      } else runFor(115000);
      require(a.rawRequest() == 0, "RAW transfer did not complete");
      require(a.rawComplete(), "RAW did not include all four remote channels");
      // Tambien debe poder solicitarse desde placa 2.
      b.command("RAW"); runFor(115000);
      require(b.rawRequest() == 0 && b.rawComplete(), "reverse RAW failed");
      if (scenario == "restart") {
        b.restartSession(); runFor(30000);
        require(a.pulse() > 0 && a.rxSequence() > 0, "new boot session rejected");
      }
    }
    std::cout << "PASS " << scenario << " route=" << a.route() << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << argv[1] << ": " << error.what() << " at " << millis() << " ms\n";
    return 1;
  }
}
