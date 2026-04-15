# Custom Bootloader Example

A simple custom bootloader that prints a countdown over UART then jumps to an application at a fixed flash address.

## What It Does

On power-up, sends "STM32F411 CUSTOM BOOTLOADER!" over UART, counts down 3 seconds, then jumps to the application at `APP_START_ADDR` (`0x0800C000`).

## Boot Flow

1. `HAL_Init()` → `SystemClock_Config()` (HSI 16 MHz)
2. Init GPIO + USART1
3. Print countdown ("Jump to App in 3s", "2s", "1s", "GO!!!")
4. `JumpToApplication()`:
   - Disable IRQs, stop SysTick, clear all NVIC pending
   - Remap system flash to `0x00000000`
   - Set MSP from app vector table
   - Call app's reset handler

## Memory Layout (`mem_layout.h`)

| Region      | Address      | Size   | Sectors |
|-------------|--------------|--------|---------|
| Bootloader  | `0x08000000` | 32 KB  | 0–1     |
| App Header  | `0x08008000` | 16 KB  | 2       |
| Application | `0x0800C000` | 464 KB | 3–7     |

## Paired With

**`custom_app`** — must be flashed at `0x0800C000`. The app sets `SCB->VTOR = APP_START_ADDR` to relocate its vector table. Both share the same `mem_layout.h`.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Bootloader entry, countdown, `JumpToApplication()` |
| `mem_layout.h` | Flash partition addresses |
| `STM32F411CEUX_FLASH.ld` | Custom linker script (bootloader occupies first 32 KB) |
