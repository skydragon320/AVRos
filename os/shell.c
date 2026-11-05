/*
 * shell.c - Serial command shell.
 *
 * The shell is not special. It is an ordinary task, spawned the same way
 * as the blinkers, and it is scheduled the same way. That is the whole
 * point of it: you can type `ps` while three other tasks are running and
 * get an answer without any of them pausing. A command loop that owned
 * the CPU would not be evidence of anything.
 *
 * When the input buffer is empty the shell blocks in uart_getc(), which
 * yields rather than spins, so an idle prompt costs nothing.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/pgmspace.h>
#include <string.h>

#include "shell.h"
#include "os.h"
#include "uart.h"
#include "gpio.h"

#define LINE_MAX   48
#define ARGV_MAX    4

/* ---- Line editing ---------------------------------------------------- */

/*
 * Read one line. Handles backspace so a typo at the prompt is not fatal,
 * and silently drops anything past LINE_MAX rather than running off the
 * end of the buffer - this task has 256 bytes of stack and no memory
 * protection behind it.
 */
static void read_line(char *buf, uint8_t max)
{
    uint8_t n = 0;

    for (;;) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_puts_P(PSTR("\r\n"));
            buf[n] = '\0';
            return;
        }
        if (c == 0x08 || c == 0x7F) {             /* backspace / delete */
            if (n > 0) {
                n--;
                uart_puts_P(PSTR("\b \b"));
            }
            continue;
        }
        if (c >= ' ' && c < 0x7F && n < max - 1) {
            buf[n++] = c;
            uart_putc(c);                          /* local echo */
        }
    }
}

/* Split on spaces in place. Returns the number of tokens found. */
static uint8_t tokenize(char *line, char *argv[], uint8_t max)
{
    uint8_t argc = 0;
    char *p = line;

    while (*p && argc < max) {
        while (*p == ' ') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') {
            p++;
        }
    }
    return argc;
}

/* Parse an unsigned decimal. Returns false on anything that is not one,
 * so `pin abc high` reports a bad argument instead of acting on pin 0. */
static bool parse_u8(const char *s, uint8_t *out)
{
    uint16_t v = 0;
    if (*s == '\0') {
        return false;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = (uint16_t)(v * 10 + (uint8_t)(*s - '0'));
        if (v > 255) {
            return false;
        }
        s++;
    }
    *out = (uint8_t)v;
    return true;
}

/* ---- Commands -------------------------------------------------------- */

static void cmd_help(void)
{
    uart_puts_P(PSTR(
        "commands:\r\n"
        "  ps                  list tasks\r\n"
        "  mem                 stack high-water marks\r\n"
        "  uptime              milliseconds since boot\r\n"
        "  suspend <id>        park a task\r\n"
        "  resume <id>         un-park a task\r\n"
        "  pin <n> out         make pin an output\r\n"
        "  pin <n> in [pullup] make pin an input\r\n"
        "  pin <n> high|low    drive an output pin\r\n"
        "  read <n>            sample an input pin\r\n"
        "  help                this list\r\n"));
}

static void print_state(os_state_t s)
{
    switch (s) {
        case OS_READY:     uart_puts_P(PSTR("ready")); break;
        case OS_SLEEP:     uart_puts_P(PSTR("sleep")); break;
        case OS_SUSPENDED: uart_puts_P(PSTR("susp "));  break;
        default:           uart_puts_P(PSTR("dead "));  break;
    }
}

/* Pad a name out to a fixed width so the columns line up. */
static void print_padded(const char *s, uint8_t width)
{
    uint8_t n = 0;
    while (*s) {
        uart_putc(*s++);
        n++;
    }
    while (n++ < width) {
        uart_putc(' ');
    }
}

static void cmd_ps(void)
{
    uart_puts_P(PSTR("id name      state\r\n"));

    for (uint8_t i = 0; i < os_task_count(); i++) {
        uart_putu16(i);
        uart_puts_P(PSTR("  "));
        print_padded(os_task_name(i), 10);
        print_state(os_task_state(i));
        if (i == os_self()) {
            uart_puts_P(PSTR("  <- you"));
        }
        uart_puts_P(PSTR("\r\n"));
    }
}

static void cmd_mem(void)
{
    uart_puts_P(PSTR("id name      used/size\r\n"));

    for (uint8_t i = 0; i < os_task_count(); i++) {
        uart_putu16(i);
        uart_puts_P(PSTR("  "));
        print_padded(os_task_name(i), 10);
        uart_putu16(os_stack_used(i));
        uart_putc('/');
        uart_putu16(os_stack_size(i));
        if (os_stack_overflowed(i)) {
            uart_puts_P(PSTR("  ** OVERFLOW **"));
        }
        uart_puts_P(PSTR("\r\n"));
    }

    uint16_t dropped = uart_rx_dropped();
    if (dropped) {
        uart_puts_P(PSTR("uart rx dropped: "));
        uart_putu16(dropped);
        uart_puts_P(PSTR("\r\n"));
    }
}

static void report_gpio(gpio_result_t r)
{
    switch (r) {
        case GPIO_OK:
            uart_puts_P(PSTR("ok\r\n"));
            break;
        case GPIO_RESERVED:
            uart_puts_P(PSTR("refused: kernel owns that pin (UART0)\r\n"));
            break;
        default:
            uart_puts_P(PSTR("refused: pin not in table\r\n"));
            break;
    }
}

static void cmd_pin(uint8_t argc, char *argv[])
{
    uint8_t pin;

    if (argc < 3) {
        uart_puts_P(PSTR("usage: pin <n> out|in|high|low\r\n"));
        return;
    }
    if (!parse_u8(argv[1], &pin)) {
        uart_puts_P(PSTR("bad pin number\r\n"));
        return;
    }

    if (strcmp(argv[2], "out") == 0) {
        report_gpio(gpio_set_output(pin));
    } else if (strcmp(argv[2], "in") == 0) {
        bool pullup = (argc >= 4 && strcmp(argv[3], "pullup") == 0);
        report_gpio(gpio_set_input(pin, pullup));
    } else if (strcmp(argv[2], "high") == 0 || strcmp(argv[2], "1") == 0) {
        report_gpio(gpio_write(pin, true));
    } else if (strcmp(argv[2], "low") == 0 || strcmp(argv[2], "0") == 0) {
        report_gpio(gpio_write(pin, false));
    } else {
        uart_puts_P(PSTR("usage: pin <n> out|in|high|low\r\n"));
    }
}

static void cmd_read(uint8_t argc, char *argv[])
{
    uint8_t pin;
    bool value;

    if (argc < 2 || !parse_u8(argv[1], &pin)) {
        uart_puts_P(PSTR("usage: read <n>\r\n"));
        return;
    }

    gpio_result_t r = gpio_read(pin, &value);
    if (r != GPIO_OK) {
        report_gpio(r);
        return;
    }
    uart_putc(value ? '1' : '0');
    uart_puts_P(PSTR("\r\n"));
}

static void cmd_suspend_resume(uint8_t argc, char *argv[], bool suspend)
{
    uint8_t id;

    if (argc < 2 || !parse_u8(argv[1], &id)) {
        uart_puts_P(PSTR("usage: <cmd> <task id>\r\n"));
        return;
    }

    bool ok = suspend ? os_suspend(id) : os_resume(id);
    if (ok) {
        uart_puts_P(PSTR("ok\r\n"));
    } else {
        uart_puts_P(PSTR("refused: no such task, or it is the idle task\r\n"));
    }
}

/* ---- Task body ------------------------------------------------------- */

void shell_task(void *arg)
{
    (void)arg;

    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    uart_puts_P(PSTR("\r\ntype 'help'\r\n"));

    for (;;) {
        uart_puts_P(PSTR("> "));
        read_line(line, LINE_MAX);

        uint8_t argc = tokenize(line, argv, ARGV_MAX);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
        } else if (strcmp(argv[0], "ps") == 0) {
            cmd_ps();
        } else if (strcmp(argv[0], "mem") == 0) {
            cmd_mem();
        } else if (strcmp(argv[0], "uptime") == 0) {
            uart_putu32(os_millis());
            uart_puts_P(PSTR(" ms\r\n"));
        } else if (strcmp(argv[0], "pin") == 0) {
            cmd_pin(argc, argv);
        } else if (strcmp(argv[0], "read") == 0) {
            cmd_read(argc, argv);
        } else if (strcmp(argv[0], "suspend") == 0) {
            cmd_suspend_resume(argc, argv, true);
        } else if (strcmp(argv[0], "resume") == 0) {
            cmd_suspend_resume(argc, argv, false);
        } else {
            uart_puts_P(PSTR("unknown command; try 'help'\r\n"));
        }
    }
}
