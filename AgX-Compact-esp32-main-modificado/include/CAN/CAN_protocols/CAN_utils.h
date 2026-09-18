#ifndef CAN_UTILS_H
#define CAN_UTILS_H

#include <stddef.h>

// Función para establecer un placeholder en un buffer de caracteres
void setPlaceholder(char* dest, size_t size, size_t placeholderWidth = 1);

#endif // CAN_UTILS_H
