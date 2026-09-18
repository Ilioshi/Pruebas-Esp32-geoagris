#pragma once
// Lamentablemente, esto no funciona porque tenemos que recompilar FreeRTOS con
// una configuración para que incluya las sytem calls que permiten obtener información
// sobre todas las tareas.
// Queda para la posteridad este código
// Todo el código fue generado por ChatGPT, tomarlo con pinzas.

void printAllTasksStackUsage();