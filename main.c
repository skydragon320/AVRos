/*
 * main.c - Demo application for the kernel.
 *
 * Spawns three tasks and starts the scheduler. What this is meant to
 * show, at the prompt:
 *
 *   - the LED keeps blinking while you type, because `blink` and `shell`
 *     are separate tasks and neither waits for the other
 *   - `ps` answers immediately and reports `shell` as the running task
 *   - `suspend 1` stops the LED; `resume 1` starts it again, without
 *     touching the LED pin directly
 *   - `mem` shows how much stack each task has ever used, measured
 *     rather than guessed
 *   - `pin 22 out` then `pin 22 high` drives a pin with no code at all
 *
 * Stacks are declared here rather than inside the kernel so that the RAM
 * cost of each task is visible at the point where the task is created.
 * On a chip with 8 KB of SRAM that is worth more than the convenience of
 * a hidden pool.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/pgmspace.h>

#include "os/os.h"
#include "os/uart.h"
#include "os/gpio.h"
#include "os/shell.h"

#define CONSOLE_BAUD  115200UL

#define LED_PIN       13      /* the Mega's built-in LED is PB7 */

/* ---- Task stacks ----------------------------------------------------- */

static uint8_t blink_stack[128];
static uint8_t heartbeat_stack[160];
static uint8_t shell_stack[256];        /* the shell formats strings */

/* ---- Tasks ----------------------------------------------------------- */

/*
 * Blink the on-board LED. The period arrives through the task argument,
 * which is the thing the fabricated r25:r24 in os_stack_init() exists to
 * make work.
 */
static void blink_task(void *arg)
{
    uint16_t period = (uint16_t)arg;
    bool on = false;

    gpio_set_output(LED_PIN);

    for (;;) {
        on = !on;
        gpio_write(LED_PIN, on);
        os_sleep(period);
    }
}

/*
 * Print the uptime every few seconds. Its only job is to interleave with
 * whatever is being typed at the prompt, which makes the point that the
 * shell is not in charge of the CPU.
 */
static void heartbeat_task(void *arg)
{
    (void)arg;

    for (;;) {
        os_sleep(5000);
        uart_puts_P(PSTR("\r\n[heartbeat] up "));
        uart_putu32(os_millis());
        uart_puts_P(PSTR(" ms\r\n> "));
    }
}

/* ---- Entry ----------------------------------------------------------- */

int main(void)
{
    uart_init(CONSOLE_BAUD);

    uart_puts_P(PSTR("\r\n\r\nbooting\r\n"));

    /* Task 0 is the idle task, created by os_start(). These become 1, 2, 3. */
    os_spawn(blink_task,     (void *)500, blink_stack,
             sizeof(blink_stack),     "blink");
    os_spawn(heartbeat_task, NULL,       heartbeat_stack,
             sizeof(heartbeat_stack), "heart");
    os_spawn(shell_task,     NULL,       shell_stack,
             sizeof(shell_stack),     "shell");

    os_start();                 /* never returns */

    return 0;                   /* unreachable; keeps the compiler happy */
}
