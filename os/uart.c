/*
 * uart.c - Interrupt-driven UART0 driver (console).
 *
 * The receive path is the interesting half. Bytes arrive whenever they
 * arrive, including in the middle of a context switch, so they are taken
 * by an interrupt and dropped into a ring buffer. The shell task then
 * reads from the buffer at its leisure.
 *
 * The ring buffer is sized to a power of two so the wrap is a mask rather
 * than a modulo - on an 8-bit chip with no divide instruction, that is
 * the difference between a couple of cycles and a couple of hundred.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "uart.h"
#include "os.h"

#define RX_BUF_SIZE 64                    /* must be a power of two */
#define RX_BUF_MASK (RX_BUF_SIZE - 1)

static volatile char     rx_buf[RX_BUF_SIZE];
static volatile uint8_t  rx_head;         /* written by the ISR          */
static volatile uint8_t  rx_tail;         /* written by the shell task   */
static volatile uint16_t rx_dropped;

/*
 * Receive complete interrupt.
 *
 * Reading UDR0 is what clears the interrupt flag, so the byte must be
 * read even when the buffer is full - otherwise this fires forever and
 * the board locks up. That is the reason for the slightly awkward shape
 * here: read first, decide whether to keep it second.
 */
ISR(USART0_RX_vect)
{
    char c = UDR0;

    uint8_t next = (rx_head + 1) & RX_BUF_MASK;
    if (next == rx_tail) {
        rx_dropped++;                     /* buffer full - byte is lost */
        return;
    }
    rx_buf[rx_head] = c;
    rx_head = next;
}

void uart_init(uint32_t baud)
{
    /* Double-speed mode halves the divisor, which gets 115200 much closer
     * to exact at 16 MHz than normal mode manages. */
    uint16_t ubrr = (uint16_t)((F_CPU / (8UL * baud)) - 1UL);

    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);

    UCSR0A = (1 << U2X0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);            /* 8 data bits   */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    rx_head = rx_tail = 0;
    rx_dropped = 0;
}

bool uart_try_getc(char *out)
{
    bool got = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (rx_head != rx_tail) {
            *out = rx_buf[rx_tail];
            rx_tail = (rx_tail + 1) & RX_BUF_MASK;
            got = true;
        }
    }
    return got;
}

char uart_getc(void)
{
    char c;
    while (!uart_try_getc(&c)) {
        /* Nothing waiting. Hand the CPU to someone who has work rather
         * than burning the rest of the time slice on an empty buffer. */
        os_yield();
    }
    return c;
}

void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) {
        /* Polled transmit. At 115200 baud one character takes about
         * 87 us, which is under a tick, so this never blocks long enough
         * to be worth an interrupt and a second ring buffer. */
    }
    UDR0 = c;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puts_P(const char *s)
{
    char c;
    while ((c = (char)pgm_read_byte(s++)) != '\0') {
        uart_putc(c);
    }
}

void uart_putu16(uint16_t v)
{
    char buf[6];
    uint8_t n = 0;

    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

void uart_putu32(uint32_t v)
{
    char buf[11];
    uint8_t n = 0;

    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

uint16_t uart_rx_dropped(void)
{
    uint16_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        v = rx_dropped;
    }
    return v;
}
