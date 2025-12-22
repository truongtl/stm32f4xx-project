#pragma once
#include <stdint.h>

typedef struct
{
    void (*putc)(uint8_t ch);
    void (*puts)(const char *s);
    void (*putsln)(const char *s);
} cli_io_t;

void cli_set_io(const cli_io_t *io);
