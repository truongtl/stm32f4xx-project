#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "cli.h"

static const cli_io_t *s_io;

static const cli_cmd_t *s_table = NULL;
static char s_line[CLI_MAX_LINE];
static int s_len = 0;

static void cli_print_prompt(void);
static int tokenize(char *line, char *argv[], int max_args);
static void execute(char *line);

/**
 * @brief Prints the CLI prompt string to the configured output backend.
 *
 * @why Provides a clear interactive cue so users know when input is expected.
 */
static void cli_print_prompt(void)
{
    if (!s_io) return;
    s_io->puts("> ");
}

/**
 * @brief Splits an input line into whitespace-delimited argument tokens.
 *
 * Replaces delimiters in-place with null terminators and stores token pointers
 * in the provided argv array until max_args is reached or input ends.
 *
 * @why Converts raw UART text into argc/argv form so command handlers can
 *      process arguments in a standard CLI style.
 *
 * @param line      Mutable input line buffer to tokenize.
 * @param argv      Output array of token pointers.
 * @param max_args  Maximum number of tokens to emit.
 * @return          Number of parsed arguments.
 */
static int tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args)
    {
        // Skip leading whitespace
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        
        // Start of token - store pointer in argv
        argv[argc++] = p;
        
        // Skip to end of token
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        
        // Null-terminate token and advance past it
        *p++ = '\0';
    }

    return argc;
}

/**
 * @brief Executes one command line by resolving and invoking a registered command.
 *
 * Parses the line into arguments, searches the registered command table by name,
 * and calls the matched handler. Emits an error when the command is unknown.
 *
 * @why Centralizes command dispatch so all input paths use identical lookup and
 *      error-report behavior.
 *
 * @param line  Mutable command line buffer to parse and execute.
 */
static void execute(char *line)
{
    if (!s_io) return;

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

/**
 * @brief Registers the CLI command table and prints the initial prompt.
 *
 * @why Binds the command set used by the interpreter before any input bytes
 *      are processed.
 *
 * @param table  Null-terminated array of commands exposed to the CLI.
 */
void cli_register(const cli_cmd_t *table)
{
    s_table = table;
    cli_print_prompt();
}

/**
 * @brief Consumes one received byte and updates CLI line-edit/dispatch state.
 *
 * Handles CR/LF line endings, backspace editing, printable character echo,
 * line buffer limits, and command execution on newline.
 *
 * @why Enables character-by-character UART-driven interaction without needing
 *      a blocking full-line input routine.
 *
 * @param ch  Incoming byte from the transport layer.
 */
void cli_process_byte(uint8_t ch) {
    if (!s_io) return;

    if (ch == '\r') return;  // Ignore carriage return
    if (ch == '\n')
    {
        s_io->puts("\r\n");  // Echo newline
        s_line[s_len] = '\0';
        execute(s_line);     // Execute completed command
        s_len = 0;
        cli_print_prompt();  // Show prompt for next command
        return;
    }
    if (ch == 0x7F || ch == 0x08)  // Backspace (DEL or BS)
    {
        if (s_len > 0)
        {
            s_len--;              // Remove character from buffer
            s_io->puts("\b \b");  // Erase character on screen
        }
        return;
    }
    if (isprint(ch))
    {
        if (s_len < (int)(sizeof(s_line)-1))
        {
            s_line[s_len++] = (char)ch;  // Add to buffer
            s_io->putc(ch);              // Echo character
        }
        else
        {
            s_io->putc('\a');  // Bell for buffer full
        }
    }
}

/**
 * @brief Sets the output interface callbacks used by the CLI core.
 *
 * @why Decouples CLI logic from hardware/transport specifics so the same core
 *      can be reused across different UART or console implementations.
 *
 * @param io  Callback table implementing putc/puts/putsln.
 */
void cli_set_io(const cli_io_t *io)
{
    s_io = io;
}

/**
 * @brief Returns the currently configured CLI output interface.
 *
 * @why Allows integration code to inspect or reuse the active I/O binding.
 *
 * @return Pointer to the active I/O callback table, or NULL if not set.
 */
const cli_io_t *cli_get_io(void)
{
    return s_io;
}
