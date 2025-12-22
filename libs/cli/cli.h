#pragma once

#ifndef CLI_MAX_LINE
#define CLI_MAX_LINE 64
#endif
#ifndef CLI_MAX_ARGS
#define CLI_MAX_ARGS 8
#endif

typedef struct
{
    const char *name;
    const char *help;
    void (*handler)(int argc, char **argv);
} cli_cmd_t;

void cli_register(const cli_cmd_t *table);
void cli_process_byte(uint8_t ch);
