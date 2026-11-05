/*
 * uart.h - Interrupt-driven UART0 driver.
 *
 * UART0 is the port wired to the Mega's USB chip, so this is the console.
 * Receive is interrupt driven into a ring buffer; transmit is polled,
 * which is simpler and good enough for a shell that prints short lines.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>
#include <avr/pgmspace.h>

void uart_init(uint32_t baud);

/* Non-blocking: returns false if nothing has arrived. */
bool uart_try_getc(char *out);

/* Blocking in the task sense: yields the CPU while waiting rather than
 * spinning on the register, so other tasks keep running. */
char uart_getc(void);

void uart_putc(char c);
void uart_puts(const char *s);

/* Print a string stored in flash. Keeps the shell's many fixed messages
 * out of the 8 KB of SRAM. Use with the PSTR() macro. */
void uart_puts_P(const char *s);

/* Small integer formatters - avr-libc's printf is far too large to pull
 * in for this. */
void uart_putu16(uint16_t v);
void uart_putu32(uint32_t v);

/* Number of received bytes dropped because the ring buffer was full.
 * Surfaced by the shell's `mem` command as a health check. */
uint16_t uart_rx_dropped(void);

#endif /* UART_H */
