# IAP Boot Example

Jumps to the STM32 built-in system bootloader (DFU/UART boot mode) when triggered by a UART command.

## What It Does

Blinks LED and listens for UART input. When byte `0x11` is received, it deinitializes all peripherals and jumps to the STM32 system bootloader at `0x1FFF0000`, enabling firmware update via ST's built-in DFU/USART protocol.

## Boot Flow

1. `HAL_Init()` → `SystemClock_Config()` (HSI 16 MHz)
2. Init GPIO (PC13 LED) + USART1 (interrupt RX)
3. Print "Jump to Bootloader when receiving 0x11!"
4. Main loop: toggle LED every 500 ms
5. On `0x11` received in `HAL_UART_RxCpltCallback`: sets `jump_requested` flag, re-arms RX interrupt
6. Main loop detects `jump_requested`:
   - Print "Jump to Bootloader ...GO!!!"
   - `JumpToBootloader()`:
     - `HAL_DeInit()` + `HAL_RCC_DeInit()`
     - Disable IRQs, stop SysTick, clear NVIC
     - Re-enable IRQs
     - Remap system flash to `0x00000000`
     - Set MSP and jump to system bootloader reset handler at `0x1FFF0000`

## System Bootloader Address

`0x1FFF0000` — STM32F411 factory-programmed system memory. The built-in bootloader supports USART1/USART2 and USB DFU.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | All logic: init, LED blink, UART RX callback, `JumpToBootloader()` |

## Linker Script

Uses default `drivers/CMSIS/Device/STM32F411CEUX_FLASH.ld` (full 512 KB flash).
