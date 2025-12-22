#include "stm32f4xx_hal.h"
#include "uart.h"

#define RX_BUF_SZ 64

extern UART_HandleTypeDef huart1;

static volatile uint8_t rx_buf[RX_BUF_SZ];
static volatile uint8_t rx_head = 0, rx_tail = 0;
static uint8_t rx_byte;

static void uart_print_raw(const char *s);

static void uart_print_raw(const char *s)
{
    while (*s)
    {
        if (*s == '\n') uart_putc('\r');
        uart_putc((uint8_t)*s++);
    }
}

void uart_start_rx(void)
{
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

uint8_t uart_getc(uint8_t *ch)
{
    if (rx_head == rx_tail) return 0;
    *ch = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SZ;
    return 1;
}

void uart_putc(uint8_t ch)
{
    HAL_UART_Transmit(&huart1, &ch, 1, HAL_MAX_DELAY);
}

void uart_print(const char *s)
{
    uart_print_raw(s);
}

void uart_println(const char *s)
{
    uart_print_raw(s);
    uart_print("\r\n");
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (rx_byte != 0x00)
        {
            uint16_t next = (rx_head + 1) % RX_BUF_SZ;

            if (next != rx_tail)
            {
                rx_buf[rx_head] = rx_byte;
                rx_head = next;
            }

            HAL_UART_Receive_IT(huart, &rx_byte, 1);
        }
    }
}
