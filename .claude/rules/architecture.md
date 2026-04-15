# Project Architecture

## Target Hardware

STM32F411CEU6 (WeAct BlackPill) — Cortex-M4F, 512 KB Flash, 128 KB RAM.

## Folder Structure

```
├── arm-none-eabi.cmake       # CMake toolchain file
├── CMakeLists.txt            # Top-level build system (auto-discovers examples + libs)
├── Makefile                  # Convenience wrapper (make <example>)
├── rules/                    # Project-wide rules (architecture, conventions, comment style)
├── drivers/
│   ├── CMSIS/
│   │   ├── Core/             # ARM CMSIS core headers (core_cm4.h, cmsis_gcc.h)
│   │   └── Device/           # STM32 device files (startup .s, linker .ld, device headers)
│   └── STM32F4xx_HAL_Driver/ # ST HAL driver library (Inc/ + Src/)
├── examples/                 # Each subdirectory is a self-contained buildable project
│   ├── cli/                  # UART command-line interface (bare-metal)
│   ├── custom_boot/          # Custom bootloader with countdown, jumps to custom_app
│   ├── custom_app/           # Application launched by custom_boot
│   ├── freertos/             # FreeRTOS with LED blink + CLI tasks
│   ├── iap_boot/             # Jump to STM32 system bootloader (DFU mode)
│   ├── ota_boot/             # A/B partition OTA bootloader with CRC + rollback
│   └── ota_factory_app/      # Factory app with OTA packet protocol (App A)
├── libs/
│   ├── cli/                  # Reusable CLI library (cli.h, cli_io.h)
│   └── freertos/             # FreeRTOS kernel V202212.00 + ARM_CM4F port
├── scripts/
│   └── memory_report.py      # Post-build Flash/RAM usage report
└── tests/
    └── ota_host.py / test_*.py  # OTA host-side test scripts
```

## Flash Memory Map (512 KB)

| Region        | Address        | Size   | Sectors |
|---------------|----------------|--------|---------|
| Bootloader    | `0x08000000`   | 32 KB  | 0–1     |
| Metadata/Hdr  | `0x08008000`   | 16 KB  | 2       |
| App A / App   | `0x0800C000`   | 208 KB | 3–5     |
| App B (OTA)   | `0x08040000`   | 256 KB | 6–7     |

Standard (non-boot) examples use the full 512 KB flash from `0x08000000`.

## Example Relationships

- **`custom_boot` + `custom_app`**: Paired bootloader/application. Bootloader at `0x08000000`, app at `0x0800C000`. Both share the same `mem_layout.h`. App sets `SCB->VTOR` to relocate its vector table.
- **`ota_boot` + `ota_factory_app`**: A/B OTA system. Bootloader at `0x08000000`, metadata at `0x08008000`, App A (factory app) at `0x0800C000`, App B (OTA target) at `0x08040000`.
- **`iap_boot`**: Standalone — jumps to STM32 system bootloader at `0x1FFF0000` on trigger byte `0x11` over UART.
- **`cli`**: Standalone bare-metal CLI demo.
- **`freertos`**: Standalone FreeRTOS demo, extends the CLI example with RTOS tasks.

## Library Auto-Discovery

CMake automatically discovers all subdirectories under `libs/`. Each library's `.c` files are compiled and its path is added to include directories. FreeRTOS is special-cased: only linked when the project name contains `"freertos"`.

## CMake Entry Points

Each example under `examples/` is a standalone project. The top-level `CMakeLists.txt` and `Makefile` select which example to build via `-DPROJECT=<name>`.
