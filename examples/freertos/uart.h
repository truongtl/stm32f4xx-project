#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

void uart_start_rx(void);
void uart_set_rx_task(TaskHandle_t task);
uint8_t uart_getc(uint8_t *ch);
void uart_putc(uint8_t ch);
void uart_print(const char *s);
void uart_println(const char *s);
