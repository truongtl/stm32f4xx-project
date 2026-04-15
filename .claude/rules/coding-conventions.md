# Coding Conventions

Applies to all code in `examples/` and `libs/` (excluding `libs/freertos/` which is a third-party kernel).

## Compiler and Language

- **Toolchain**: `arm-none-eabi-gcc`
- **C standard**: `gnu11`
- **Optimization**: `-O0` with `-g3` debug info
- **CPU flags**: `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- **Compile defines**: `STM32F411xE`, `USE_HAL_DRIVER`

## Hardware Defaults (all examples)

| Peripheral | Configuration |
|-----------|---------------|
| UART | USART1, PA9 (TX) / PA10 (RX), 115200 baud |
| LED | PC13, active-low (WeAct BlackPill) |
| Clock | HSI 16 MHz, no PLL, `FLASH_LATENCY_0` |
| Startup | `drivers/CMSIS/Device/startup_stm32f411xe.s` |

## Per-Example Required Files

Every example directory must contain:

- `main.c` — entry point and peripheral init
- `stm32f4xx_hal_conf.h` — HAL module selection (enable only modules the example needs)
- `stm32f4xx_it.c` — IRQ handlers
- `syscalls.c` — newlib stubs (`_write`, `_read`, `_sbrk`, etc.)
- `system_stm32f4xx.c` — system clock setup

## Linker Script Priority

The build system selects the linker script in this order:
1. Project-specific `.ld` in the example directory (e.g. `examples/ota_boot/STM32F411CEUX_FLASH.ld`)
2. Fallback: `drivers/CMSIS/Device/STM32F411CEUX_FLASH.ld` (full 512 KB, starts at `0x08000000`)

Use a custom linker script only when the example must start at a non-default flash address.

## Bootloader / App Jump Sequence

When a bootloader jumps to an application:
1. Disable all IRQs (`__disable_irq()`)
2. Stop SysTick (write 0 to `SysTick->CTRL`)
3. Clear all NVIC pending bits
4. Remap system flash (`SYSCFG->MEMRMP = 0`)
5. Set MSP from app vector table (`__set_MSP(*(uint32_t *)app_addr)`)
6. Call app reset handler (`((void(*)())(*(uint32_t *)(app_addr + 4)))()`)

When an application is launched by a bootloader:
1. `HAL_Init()` first
2. Relocate vector table: `SCB->VTOR = APP_START_ADDR`
3. `__enable_irq()` (bootloader leaves IRQs disabled)
4. Then `SystemClock_Config()` and peripheral inits

## Shared Header Files

For paired examples (`custom_boot`/`custom_app`, `ota_boot`/`ota_factory_app`), memory layout constants live in a shared `mem_layout.h` inside each example directory. Both directories carry a copy — keep them in sync.

## OTA Metadata

`ota_metadata.h` is shared between `ota_boot` and `ota_factory_app`. Both directories carry a copy — keep them in sync.

## Interrupt-Driven UART RX

The interrupt-driven RX pattern used in all UART examples:
- Enable `HAL_UART_Receive_IT()` for 1 byte
- In `HAL_UART_RxCpltCallback`: push byte to ring buffer, re-arm `HAL_UART_Receive_IT()`
- Main loop (or RTOS task) polls/reads the ring buffer

## Error Handling

- `Error_Handler()`: infinite loop with optional LED blink — never returns
- Peripheral init failures call `Error_Handler()`
- Do not use `assert_param` in production code (increases code size)

## Volatile for Shared Flags

Any variable written in an ISR and read in main/task context must be declared `volatile`:
```c
volatile uint8_t jump_requested = 0;
```
