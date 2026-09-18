#pragma once
#include "DiagnosticProtocol.h"

// Pure protocol regression checks, also runnable on a host compiler.
// Return 0 on success, otherwise the 1-based failing check.
inline unsigned diagnosticProtocolSelfTest() {
    using namespace DiagnosticProtocol;
    const std::string request = ">STX08," + std::string(23, '0') + "<";
    const std::string reply = ">RTX08," + std::string(23, '0') + ";ID=2875246;*56<";
    if (!validFrame(reply)) return 1; // Real user-captured TRAX reply.
    if (!validFrame(">RRS0 115200,C;ID=2875246;*37<")) return 2;
    if (validFrame(">RRS0 115200,C;ID=2875246;*00<")) return 3;
    if (validFrame(reply.substr(0, reply.size() - 1))) return 4;
    if (!responseMatches(request, reply)) return 5;
    if (responseMatches(request, ">RTX18,DUT18v1<")) return 6;
    if (!responseMatches(request, ">ETX:PARAM<")) return 7;
    if (!responseMatches(request, ">RPW?<")) return 8;
    if (!stx08Acknowledged(request, reply)) return 9;
    if (stx08Acknowledged(request, ">ETX:PARAM<")) return 10;
    if (stx08Acknowledged(request, ">RTX08," + std::string(23, '1') + "<")) return 11;
    if (stx08Acknowledged(request, ">RTX18,DUT18v1<")) return 12;
    if (stx08Acknowledged(request, "")) return 13;
    const std::string unsafe = command("DUT,>STX08,123<;{x}\\\r\n");
    if (unsafe.front() != '>' || unsafe.back() != '<') return 14;
    if (unsafe.substr(1, unsafe.size() - 2).find_first_of("><;{}\\\r\n") != std::string::npos) return 15;
    if (command(std::string(240, 'a')).size() != 248) return 16;
    if (command(std::string(241, 'a')).size() != 248) return 17;
    if (command(std::string(241, 'a')).find("!TRUNC<") == std::string::npos) return 18;
    if (hex(">RTX08<", 64) != "3E52545830383C") return 19;
    if (hex("ABCD", 2) != "4142") return 20;
    if (!responseMatches(">QUS07<", ">RUS07,123<")) return 21;
    if (responseMatches(">QUS07<", ">RUS08,123<")) return 22;
    if (!responseMatches(">SCC26R<", ">RCC260001000000<")) return 23;
    return 0;
}
