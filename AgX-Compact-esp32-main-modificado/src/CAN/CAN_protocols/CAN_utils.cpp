#include "CAN/CAN_protocols/CAN_utils.h"
#include <algorithm>

/************************************************************************************
*                                 Public Functions                                  *
*************************************************************************************/

void setPlaceholder(char* dest, size_t size, size_t placeholderWidth) {
    if (size == 0) {
        return;
    }
    size_t width = std::min(placeholderWidth, size - 1);
    std::fill_n(dest, width, '~');
    dest[width] = '\0';
}
