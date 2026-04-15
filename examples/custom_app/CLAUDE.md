# Custom App Example

Application designed to be launched by the `custom_boot` bootloader. Runs from a non-default flash address with relocated vector table.

## What It Does

Prints "STM32F411 APPLICATION!" over UART, then blinks the LED at 500 ms intervals. Demonstrates `SCB->VTOR` relocation for boot-from-offset operation.

## Boot Flow

1. `HAL_Init()`
2. `SCB->VTOR = APP_START_ADDR` — relocates vector table to `0x0800C000`
3. `__enable_irq()` — re-enables interrupts (disabled by bootloader)
4. `SystemClock_Config()` → GPIO + USART1 init
5. Main loop: toggle PC13 LED every 500 ms

## Memory Layout (`mem_layout.h`)

Same as `custom_boot`:

| Region      | Address      | Size   | Sectors |
|-------------|--------------|--------|---------|
| Bootloader  | `0x08000000` | 32 KB  | 0–1     |
| App Header  | `0x08008000` | 16 KB  | 2       |
| Application | `0x0800C000` | 464 KB | 3–7     |

## Paired With

**`custom_boot`** — the bootloader that jumps here. This app must be flashed at `0x0800C000`.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | App entry with VTOR relocation, LED blink |
| `mem_layout.h` | Flash partition addresses (shared with custom_boot) |
| `STM32F411CEUX_FLASH.ld` | Custom linker script placing code at `0x0800C000` |
