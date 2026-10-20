#pragma once
#define UART_NUM_1 1
#define ESP_OK 0
inline int uart_wait_tx_done(int, int) { return ESP_OK; }
