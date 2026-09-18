#include "SendTaskPressure.h"
#include "agx_utils.h"

std::string SendTaskPressure::buildStx09(const PressureData& data) {
    if (!data.valid) {
        return ">STX09,P~~<";
    }
    std::string base64Value = toBase64UrlSafe(data.pressure, 0, 100, 2);
    return std::string(">STX09,P") + base64Value + "<";
}

