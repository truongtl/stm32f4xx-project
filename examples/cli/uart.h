#pragma once

void uart_start_rx(void);
uint8_t uart_getc(uint8_t *ch);
void uart_putc(uint8_t ch);
void uart_print(const char *s);
void uart_println(const char *s);
