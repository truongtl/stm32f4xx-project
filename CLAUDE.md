# STM32F4xx Project

Bare-metal and RTOS example projects for the STM32F411CEU6 (Cortex-M4F, 512 KB Flash, 128 KB RAM).

## Build System

- **Toolchain**: `arm-none-eabi-gcc` (GNU ARM Embedded), configured in `arm-none-eabi.cmake`
- **Build**: CMake 3.20+ with Ninja (fallback: Unix Makefiles)
- **CPU flags**: `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- **C standard**: `gnu11`, optimization `-O0`, debug `-g3`

### Build Commands

```bash
# Build a single example
make <example_name>        # e.g. make cli, make freertos

# Build all examples
make all

# Clean
make clean                 # clean all
make clean <example_name>  # clean specific example

# List available examples
make list

# CMake directly
cmake -S . -B build/<name> -G Ninja -DPROJECT=<name> -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
cmake --build build/<name> --config Release
```

### Build Artifacts

Located at `build/<project>/out/`:
- `<project>.elf` — ELF binary
- `<project>.hex` — Intel HEX
- `<project>.bin` — Raw binary
- `<project>.map` — Linker map

### Memory Report

Post-build runs `scripts/memory_report.py` (requires Python 3) which parses `arm-none-eabi-size` output and displays Flash/RAM usage with progress bars. Falls back to basic `size` output if Python is not available.

## Project Rules

Architecture, coding conventions, and comment style are in `.claude/rules/`:
- `.claude/rules/architecture.md` — project structure, flash map, example relationships, library auto-discovery
- `.claude/rules/coding-conventions.md` — C coding conventions for `examples/` and `libs/` (excluding `libs/freertos/`)
- `.claude/rules/comment-style.md` — function documentation and inline comment rules

## Per-Directory Documentation

Each example and library has its own `CLAUDE.md` with boot flow, key files, and example-specific details:

| Path | Contents |
|------|----------|
| `examples/cli/CLAUDE.md` | UART CLI bare-metal example |
| `examples/custom_boot/CLAUDE.md` | Custom bootloader with countdown |
| `examples/custom_app/CLAUDE.md` | Companion app for custom_boot |
| `examples/freertos/CLAUDE.md` | FreeRTOS LED blink + CLI tasks |
| `examples/iap_boot/CLAUDE.md` | Jump to STM32 system bootloader (DFU) |
| `examples/ota_boot/CLAUDE.md` | A/B partition OTA bootloader |
| `examples/ota_factory_app/CLAUDE.md` | Factory app with OTA packet protocol |
| `libs/cli/CLAUDE.md` | CLI library API and usage |
