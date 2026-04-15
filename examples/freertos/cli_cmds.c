#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cli.h"
#include "cli_cmds.h"

__attribute__((weak)) void board_led_write(int led_state) { (void)led_state; }
__attribute__((weak)) sys_time_t board_gettime(void) { return (sys_time_t){0}; }
__attribute__((weak)) void board_settime(sys_time_t t) { (void)t; }
__attribute__((weak)) void board_reboot(void) {}

static void cmd_help(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_led(int argc, char **argv);
static void cmd_gettime(int argc, char **argv);
static void cmd_settime(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);

static const cli_cmd_t s_cmds[] = {
  { "help",   "List commands",            cmd_help   },
  { "echo",   "Echo text",                cmd_echo   },
  { "led",    "LED <on|off>",              cmd_led    },
  { "gettime", "Get clock",                cmd_gettime },
  { "settime", "Set clock",               cmd_settime },
  { "reboot", "Software reset",           cmd_reboot },
  { NULL,     NULL,                       NULL       }
};

static const cli_cmd_t* get_table(void)
{
    return s_cmds;
}

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    const cli_io_t *io = cli_get_io();
    io->putsln("Commands:");

    for (const cli_cmd_t *cmd = get_table(); cmd != NULL && cmd->name != NULL; ++cmd)
    {
        io->puts("  ");
        io->puts(cmd->name);
        if (cmd->help)
        {
            io->puts(" - ");
            io->putsln(cmd->help);
        }
        else
        {
            io->putsln("");
        }
    }
}

static void cmd_echo(int argc, char **argv) {
    const cli_io_t *io = cli_get_io();
    for (int i = 1; i < argc; ++i)
    {
        io->puts(argv[i]);

        if (i + 1 < argc)
        {
            io->putc(' ');
        }
    }

    io->putsln("");
}

static void cmd_led(int argc, char **argv)
{
    const cli_io_t *io = cli_get_io();
    if (argc < 2)
    {
        io->putsln("Usage: LED <on|off>");
        return;
    }

    int led_state;
    if (strcmp(argv[1], "on") == 0)
    {
        led_state = 1;
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        led_state = 0;
    }
    else
    {
        io->putsln("Usage: led <on|off>");
        return;
    }
    board_led_write(!led_state);
    io->putsln(led_state ? "LED ON" : "LED OFF");
}

static void cmd_gettime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const cli_io_t *io = cli_get_io();

    sys_time_t t = board_gettime();

    char buf[32];
    sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u",
            t.year, t.month, t.day,
            t.hour, t.min, t.sec);

    io->puts("Clock: ");
    io->putsln(buf);
}

static void cmd_settime(int argc, char **argv) {
    const cli_io_t *io = cli_get_io();
    if (argc != 3)
    {
        io->putsln("Usage: settime yyyy-mm-dd hh:mm:ss");
        return;
    }

    int y, m, d, H, M, S;
    sys_time_t t;

    if (sscanf(argv[1], "%d-%d-%d", &y, &m, &d) != 3 ||
        sscanf(argv[2], "%d:%d:%d", &H, &M, &S) != 3)
    {
        io->putsln("Invalid date/time format!");
        return;
    }

    if (y < 2000 || y > 2099 ||
        m < 1 || m > 12 ||
        d < 1 || d > 31 ||
        H < 0 || H > 23 ||
        M < 0 || M > 59 ||
        S < 0 || S > 59)
    {
        io->putsln("Invalid date/time values!");
        return;
    }

    t.year   = (uint16_t)y;
    t.month  = (uint8_t)m;
    t.day    = (uint8_t)d;
    t.hour   = (uint8_t)H;
    t.min    = (uint8_t)M;
    t.sec    = (uint8_t)S;

    board_settime(t);

    char buf[32];
    sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u", y, m, d, H, M, S);
    io->puts("Clock set to ");
    io->putsln(buf);
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cli_get_io()->putsln("Reboot...");
    board_reboot();
}

/**
 * @brief Registers the default CLI command table.
 *
 * @why Provides a standard set of commands (help, echo, led, time, reboot)
 *      for basic board interaction and testing under FreeRTOS.
 */
void cli_register_default_table(void)
{
    cli_register(s_cmds);
}
