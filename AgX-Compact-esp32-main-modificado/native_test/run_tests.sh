#!/bin/bash

# Script para compilar y ejecutar las pruebas de base64 en forma local
# Sin necesidad de dispositivo físico ESP32

# Colores para una mejor visualización
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Compilando pruebas de Base64 URL-safe ===${NC}"

# Compilar el programa de prueba
g++ -std=c++11 -o base64_test base64_test.cpp -lm

# Verificar si la compilación fue exitosa
if [ $? -ne 0 ]; then
    echo -e "${RED}Error en la compilación${NC}"
    exit 1
fi

echo -e "${GREEN}Compilación exitosa${NC}"
echo -e "${BLUE}=== Ejecutando pruebas ===${NC}"

# Ejecutar el programa de prueba
./base64_test

# Verificar si la ejecución fue exitosa
if [ $? -ne 0 ]; then
    echo -e "${RED}Error en la ejecución de las pruebas${NC}"
    exit 1
fi

echo -e "${GREEN}=== Todas las pruebas completadas exitosamente ===${NC}"