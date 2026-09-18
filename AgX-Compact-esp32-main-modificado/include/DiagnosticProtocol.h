#pragma once

#include <string>

// Helpers independent of Arduino: diagnostics must never embed a GAP command.
namespace DiagnosticProtocol {
inline std::string command(const std::string& text) {
    std::string result = ">STX18,";
    const size_t limit = 240; // TX18 capacity is 256 bytes; leave headroom.
    const size_t count = text.size() <= limit ? text.size() : limit - 6;
    for (size_t i = 0; i < count; ++i) {
        const unsigned char c = text[i];
        result += (c < 32 || c > 126 || c == '>' || c == '<' ||
                   c == ';' || c == '{' || c == '}' || c == '\\') ? '_' : c;
    }
    if (text.size() > limit) result += "!TRUNC";
    result += '<';
    return result;
}

inline bool validFrame(const std::string& frame) {
    if (frame.size() < 5 || frame.front() != '>' || frame.back() != '<') return false;
    const size_t checksum = frame.find(";*");
    if (checksum == std::string::npos) return true; // Optional in GAP.
    if (checksum + 4 != frame.size() - 1) return false;
    unsigned char value = 0;
    for (size_t i = 0; i <= checksum + 1; ++i) value ^= static_cast<unsigned char>(frame[i]);
    const char* digits = "0123456789ABCDEF";
    return frame[checksum + 2] == digits[value >> 4] && frame[checksum + 3] == digits[value & 15];
}

inline bool responseMatches(const std::string& request, const std::string& response) {
    if (request.size() < 5 || !validFrame(response)) return false;
    if (response.compare(0, 5, ">RPW?") == 0) return true;
    const std::string code = request.substr(2, 2);
    if (response.compare(0, 2, ">E") == 0) return response.compare(2, 2, code) == 0;
    std::string expected = ">R" + code;
    if ((code == "TX" || code == "US" || code == "UC" || code == "CC") && request.size() >= 7)
        expected += request.substr(4, 2);
    return response.compare(0, expected.size(), expected) == 0;
}

inline bool stx08Acknowledged(const std::string& request, const std::string& response) {
    if (request.compare(0, 7, ">STX08,") != 0 || request.size() != 31 ||
        request.back() != '<' || !validFrame(response)) return false;
    std::string expected = request.substr(0, request.size() - 1);
    expected[1] = 'R';
    return response.compare(0, expected.size(), expected) == 0 &&
           (response[expected.size()] == ';' || response[expected.size()] == '<');
}

inline std::string hex(const std::string& input, size_t maxBytes) {
    const char* digits = "0123456789ABCDEF";
    std::string result;
    for (size_t i = 0; i < input.size() && i < maxBytes; ++i) {
        const unsigned char c = input[i];
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result;
}
}
