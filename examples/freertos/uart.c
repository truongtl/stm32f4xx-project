#include "stm32f4xx_hal.h"
#include "uart.h"

#define RX_BUF_SZ 64

extern UART_HandleTypeDef huart1;

static volatile uint8_t rx_buf[RX_BUF_SZ];
static volatile uint8_t rx_head = 0, rx_tail = 0;
static volatile uint8_t rx_byte;
static TaskHandle_t rx_task = NULL;

static void uart_print_raw(const char *s);

/**
 * @brief Transmits a string raw, inserting CR before each LF.
 *
 * @why Ensures correct CRLF line endings for terminal emulators when
 *      printing strings that use newline characters.
 *
 * @param s Null-terminated string to transmit.
 */
static void uart_print_raw(const char *s)
{
    while (*s)
    {
        if (*s == '\n') uart_putc('\r');
        uart_putc((uint8_t)*s++);
    }
}

/**
 * @brief Starts UART receive interrupt mode.
 *
 * @why Enables asynchronous UART input so the CPU is not blocked waiting
 *      for data; received bytes are buffered by the ISR.
 */
void uart_start_rx(void)
{
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);
}

/**
 * @brief Registers the FreeRTOS task handle to notify on received UART data.
 *
 * @why Allows the UART ISR to wake the CLI task via task notification instead
 *      of polling, enabling efficient blocking receive in FreeRTOS.
 *
 * @param task Handle of the task to notify when data arrives.
 */
void uart_set_rx_task(TaskHandle_t task)
{
    rx_task = task;
}

/**
 * @brief Retrieves a character from the UART receive buffer.
 *
 * @why Provides non-blocking access to buffered UART data for the CLI task.
 *
 * @param ch Pointer to store the retrieved character.
 * @return 1 if a character was available, 0 if the buffer is empty.
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
 * @why Provides the base output primitive for CLI responses.
 *
 * @param ch Character to transmit.
 */
void uart_putc(uint8_t ch)
{
    HAL_UART_Transmit(&huart1, &ch, 1, HAL_MAX_DELAY);
}

/**
 * @brief Transmits a null-terminated string over UART.
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
 * @brief Transmits a null-terminated string followed by CRLF over UART.
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
 * @brief UART error callback that re-arms the receive interrupt.
 *
 * @why Recovers from UART framing/overrun errors without permanently
 *      losing UART receive functionality.
 *
 * @param huart UART handle pointer.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        HAL_UART_Receive_IT(huart, (uint8_t *)&rx_byte, 1U);
    }
}

/**
 * @brief UART receive complete callback that buffers data and notifies CLI task.
 *
 * Stores the received byte in the circular buffer, re-arms receive interrupt,
 * then sends a task notification to wake the CLI task.
 *
 * @why Handles UART interrupts safely from ISR context and triggers the
 *      FreeRTOS CLI task to process the incoming data.
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

        HAL_UART_Receive_IT(huart, (uint8_t *)&rx_byte, 1);

        if (rx_task != NULL)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(rx_task, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}
