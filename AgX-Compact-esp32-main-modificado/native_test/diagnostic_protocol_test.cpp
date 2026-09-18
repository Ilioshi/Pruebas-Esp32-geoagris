#include "../include/DiagnosticProtocolSelfTest.h"
#include <cstdio>

int main() {
    const unsigned failure = diagnosticProtocolSelfTest();
    std::printf("DiagnosticProtocol: %s (check=%u)\n", failure ? "FAIL" : "PASS", failure);
    return failure ? 1 : 0;
}
