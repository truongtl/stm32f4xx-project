#include "stm32f4xx_hal.h"
#include "uart.h"

#define RX_BUF_SZ 64

extern UART_HandleTypeDef huart1;

static volatile uint8_t rx_buf[RX_BUF_SZ];
static volatile uint8_t rx_head = 0, rx_tail = 0;
static volatile uint8_t rx_byte;

static void uart_print_raw(const char *s);

static void uart_print_raw(const char *s)
{
    while (*s != '\0')
    {
        if (*s == '\n') uart_putc('\r');
        uart_putc((uint8_t)*s++);
    }
}

/**
 * @brief Starts UART receive interrupt mode.
 *
 * @why Enables asynchronous UART input for CLI command processing
 *      without blocking the main loop.
 */
void uart_start_rx(void)
{
    if (HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1) != HAL_OK)
    {
        while (1) {}
    }
}

/**
 * @brief Retrieves a character from the UART receive buffer.
 *
 * Returns the next available character if the buffer is not empty.
 *
 * @why Provides non-blocking access to received UART data for CLI input.
 *
 * @param ch Pointer to store the retrieved character.
 * @return 1 if character was available, 0 if buffer empty.
 */
uint8_t uart_getc(uint8_t *ch)
{
    if (rx_head == rx_tail) return 0;
    *ch = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SZ;
    return 1;
}

/**
 * @brief Transmits a single character over UART.
 *
 * @why Provides basic UART output for CLI responses and debugging.
 *
 * @param ch Character to transmit.
 */
void uart_putc(uint8_t ch)
{
    HAL_UART_Transmit(&huart1, &ch, 1, 10);
}

/**
 * @brief Transmits a string over UART.
 *
 * @why Enables text output for CLI command responses.
 *
 * @param s Null-terminated string to transmit.
 */
void uart_print(const char *s)
{
    uart_print_raw(s);
}

/**
 * @brief Transmits a string followed by CRLF over UART.
 *
 * @why Provides line-terminated output for CLI command responses.
 *
 * @param s Null-terminated string to transmit.
 */
void uart_println(const char *s)
{
    uart_print_raw(s);
    uart_print("\r\n");
}

/**
 * @brief UART receive complete callback that buffers incoming data.
 *
 * Stores received byte in circular buffer and re-arms reception.
 *
 * @why Handles UART interrupts to buffer incoming CLI input bytes
 *      for later processing by the main loop.
 *
 * @param huart UART handle pointer.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint16_t next = (rx_head + 1) % RX_BUF_SZ;

        if (next != rx_tail)
        {
            rx_buf[rx_head] = rx_byte;
            rx_head = next;
        }

        if (HAL_UART_Receive_IT(huart, (uint8_t *)&rx_byte, 1) != HAL_OK)
        {
            HAL_UART_AbortReceive(huart);
            (void)HAL_UART_Receive_IT(huart, (uint8_t *)&rx_byte, 1);
        }
    }
}
