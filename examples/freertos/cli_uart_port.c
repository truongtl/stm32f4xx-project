#include "cli_io.h"
#include "uart.h"
#include "cli_uart_port.h"

static void uart_cli_putc(uint8_t ch)
{
    uart_putc(ch);
}

static void uart_cli_puts(const char *s)
{
    uart_print(s);
}

static void uart_cli_putsln(const char *s)
{
    uart_println(s);
}

static const cli_io_t cli_uart_io =
{
    .putc = uart_cli_putc,
    .puts = uart_cli_puts,
    .putsln = uart_cli_putsln,
};

/**
 * @brief Binds the CLI I/O interface to UART functions.
 *
 * @why Connects the CLI library to UART output for command responses
 *      and user interaction in the FreeRTOS CLI example.
 */
void cli_bind_uart(void)
{
    cli_set_io(&cli_uart_io);
}
