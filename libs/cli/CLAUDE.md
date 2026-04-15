# CLI Library

Lightweight reusable command-line interpreter for bare-metal and RTOS environments.

## API (`cli.h`)

```c
void cli_register(const cli_cmd_t *table);
void cli_process_byte(uint8_t ch);
```

- `cli_register(table)` — registers a null-terminated array of `cli_cmd_t` entries
- `cli_process_byte(ch)` — feeds one received character into the parser; executes a command when `\r` or `\n` is received

## I/O Binding (`cli_io.h`)

```c
void cli_set_io(const cli_io_t *io);
```

`cli_io_t` contains three function pointers:
- `putc(ch)` — send a single character
- `puts(str)` — send a null-terminated string
- `putsln(str)` — send a string followed by `\r\n`

Call `cli_set_io()` once at startup before processing any bytes.

## Command Table Format

```c
static const cli_cmd_t cmd_table[] = {
    { "help",  "List commands",      cmd_help  },
    { "echo",  "Echo arguments",     cmd_echo  },
    { NULL,    NULL,                 NULL      },  // terminator
};

cli_register(cmd_table);
```

## Limits

| Constant | Default | Description |
|----------|---------|-------------|
| `CLI_MAX_LINE` | 64 | Maximum input line length (bytes) |
| `CLI_MAX_ARGS` | 8 | Maximum argument count per command |

## Command Handler Signature

```c
typedef void (*cli_cmd_fn_t)(int argc, char **argv);
```

`argv[0]` is the command name. Arguments are separated by whitespace.

## Usage in Examples

Used by `examples/cli/` and `examples/freertos/`. Each example provides:
- `cli_uart_port.c` — creates a `cli_io_t` wired to the UART driver and calls `cli_set_io()`
- `cli_cmds.c` — defines the command table with weak-linked board-specific handlers
- `cli_cmd_handle.c` — implements the weak board-specific functions (`board_led_write`, `board_gettime`, etc.)
