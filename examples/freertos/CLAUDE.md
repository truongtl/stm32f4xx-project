# FreeRTOS Example

FreeRTOS-based application with two tasks: LED blink and CLI processing.

## What It Does

Runs FreeRTOS with two tasks:
- **LED_Task** (priority 1): Toggles PC13 every 500 ms
- **CLI_Task** (priority 2): Blocks on task notification from UART ISR, drains the RX ring buffer into the CLI parser

Same CLI commands as the `cli` example (help, echo, led, gettime, settime, reboot).

## Boot Flow

1. `HAL_Init()` → `SystemClock_Config()` (HSI 16 MHz)
2. Init GPIO + USART1 (interrupt-driven RX)
3. `uart_start_rx()` → `cli_bind_uart()` → `cli_register_default_table()`
4. Create tasks: `vLedTask` (stack 128 words), `vCliTask` (stack 256 words)
5. `uart_set_rx_task(cli_task_handle)` — UART ISR will `xTaskNotify` the CLI task on RX
6. `vTaskStartScheduler()` — FreeRTOS takes over

## FreeRTOS Configuration (`FreeRTOSConfig.h`)

- Preemptive scheduling
- Heap implementation selectable at build time via `-DconfigHEAP_IMPLEMENTATION=N` (default: `heap_4`)
- Kernel source: `libs/freertos/` with ARM_CM4F port

## Build

```bash
make freertos
# With specific heap:
cmake -S . -B build/freertos -G Ninja -DPROJECT=freertos -DconfigHEAP_IMPLEMENTATION=4
```

FreeRTOS is auto-linked only when the project name contains `"freertos"`.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Task creation (`vLedTask`, `vCliTask`), scheduler start |
| `FreeRTOSConfig.h` | RTOS kernel configuration |
| `cli_cmds.c` / `cli_cmds.h` | Command table (same structure as cli example) |
| `cli_uart_port.c` / `cli_uart_port.h` | CLI-to-UART binding |
| `uart.c` / `uart.h` | UART driver with interrupt RX and task notification |

## Linker Script

Uses default `drivers/CMSIS/Device/STM32F411CEUX_FLASH.ld` (full 512 KB flash).
