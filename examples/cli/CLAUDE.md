# CLI Example

UART-based command-line interface running on bare metal (no RTOS).

## What It Does

Receives characters over USART1, parses commands, and executes handlers. Uses the reusable `libs/cli/` library with UART I/O bindings.

## Boot Flow

1. `HAL_Init()` → `SystemClock_Config()` (HSI 16 MHz)
2. Init GPIO (PC13 LED) and USART1 (PA9/PA10, 115200)
3. `uart_start_rx()` — enables interrupt-driven UART receive
4. `cli_bind_uart()` — binds CLI output to UART (`cli_uart_port.c`)
5. `cli_register_default_table()` — registers command table
6. Main loop: polls `uart_getc()`, feeds bytes to `cli_process_byte()`

## Available Commands

| Command   | Description                              |
|-----------|------------------------------------------|
| `help`    | List all commands                        |
| `echo`    | Echo back arguments                      |
| `led`     | LED on/off (`led 0` or `led 1`)          |
| `gettime` | Print current system time                |
| `settime` | Set time (`settime yyyy-mm-dd hh:mm:ss`) |
| `reboot`  | Software reset                           |

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Entry point, peripheral init, main loop |
| `cli_cmds.c` / `cli_cmds.h` | Command table and handlers; uses weak symbols for `board_led_write`, `board_gettime`, `board_settime`, `board_reboot` |
| `cli_uart_port.c` / `cli_uart_port.h` | Binds CLI I/O to the UART driver |
| `uart.c` / `uart.h` | UART driver with interrupt RX ring buffer |
| `cli_cmd_handle.c` | Board-specific implementations of the weak functions |

## Linker Script

Uses default `drivers/CMSIS/Device/STM32F411CEUX_FLASH.ld` (full 512 KB flash from `0x08000000`).
