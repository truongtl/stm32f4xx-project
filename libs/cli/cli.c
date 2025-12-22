#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "cli.h"
#include "cli_io.h"

static const cli_io_t *s_io;

static const cli_cmd_t *s_table = NULL;
static char s_line[CLI_MAX_LINE];
static int s_len = 0;

static void cli_print_prompt(void);
static int tokenize(char *line, char *argv[], int max_args);
static void execute(char *line);

static void cli_print_prompt(void)
{
    s_io->puts("> ");
}

static int tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args)
    {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        *p++ = '\0';
    }

    return argc;
}

static void execute(char *line)
{
    char *argv[CLI_MAX_ARGS];
    int argc = tokenize(line, argv, CLI_MAX_ARGS);
    if (argc == 0) return;

    for (const cli_cmd_t *c = s_table; c && c->name; ++c)
    {
        if (strcmp(argv[0], c->name) == 0)
        {
            c->handler(argc, argv);
            return;
        }
    }

    s_io->putsln("ERR: unknown cmd (try 'help')");
}

void cli_register(const cli_cmd_t *table)
{
    s_table = table;
    cli_print_prompt();
}

void cli_process_byte(uint8_t ch) {
    if (ch == '\r') return;
    if (ch == '\n')
    {
        s_io->puts("\r\n");
        s_line[s_len] = '\0';
        execute(s_line);
        s_len = 0;
        cli_print_prompt();
        return;
    }
    if (ch == 0x7F || ch == 0x08)
    {
        if (s_len > 0)
        {
            s_len--;
            s_io->puts("\b \b");
        }
        return;
    }
    if (isprint(ch))
    {
        if (s_len < (int)(sizeof(s_line)-1))
        {
            s_line[s_len++] = (char)ch;
            s_io->putc(ch);
        }
    }
}

void cli_set_io(const cli_io_t *io)
{
    s_io = io;
}
