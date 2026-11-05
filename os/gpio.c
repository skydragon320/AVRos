/*
 * gpio.c - Pin access by Arduino pin number, without the Arduino core.
 *
 * WHY THIS FILE EXISTS
 *
 * Nothing in a bare-metal build knows that "pin 13" means anything. The
 * Arduino core carries a table mapping its pin numbers onto the chip's
 * ports and bits; with no core, the kernel needs its own.
 *
 * WHY THE WRITES ARE IN ATOMIC BLOCKS
 *
 * Setting one pin is a read-modify-write on a whole port register: read
 * PORTA, flip one bit, write PORTA back. If the scheduler preempts
 * between the read and the write, and the task that runs next also
 * touches PORTA, one of the two updates is silently lost. That is a
 * genuine race on genuinely shared state, created by the kernel's own
 * preemption, and it has to be closed by hand.
 *
 * AVR does have SBI and CBI, which set or clear a single bit atomically
 * in one instruction - but they can only reach I/O addresses 0x00-0x1F.
 * On the Mega the later ports live in extended I/O space, out of reach,
 * so a uniform solution has to be the lock. Disabling interrupts for
 * three instructions is a small enough hammer.
 *
 * STATUS OF THE TABLE
 *
 * Partial on purpose. It covers the pins used by the demo and the two
 * the kernel reserves; the Mega has 70 and the remainder are mechanical
 * to add. Typing the rest out by hand is an invitation to transpose a
 * bit number, so the plan is to generate them from the Arduino variant
 * file (hardware/arduino/avr/variants/mega/pins_arduino.h) rather than
 * copy them by eye.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/io.h>
#include <util/atomic.h>

#include "gpio.h"

/*
 * Pins 0 and 1 are the UART0 receive and transmit lines, which is to say
 * the console this kernel talks to. Reconfiguring them kills the shell
 * mid-session with no error and no way to ask what happened.
 *
 * Worth being honest about what this check is: it lives in the shell's
 * command path, so it stops a typo at the prompt. It does nothing to a
 * task that writes PORTE directly. With no MMU there is no mechanism
 * that could - the protection is a convention, not a boundary.
 */
bool gpio_is_reserved(uint8_t pin)
{
    return (pin == 0 || pin == 1);
}

/*
 * Resolve an Arduino pin number to a port and bit.
 *
 * On AVR the three registers of a port are consecutive in memory, laid
 * out PINx, DDRx, PORTx. Returning the PORTx address alone is therefore
 * enough: the other two are at -1 and -2.
 */
static bool pin_lookup(uint8_t pin, volatile uint8_t **port, uint8_t *bit)
{
    if (pin <= 1) {                       /* D0, D1 -> PE0, PE1 */
        *port = &PORTE;
        *bit  = pin;
        return true;
    }
    if (pin >= 10 && pin <= 13) {         /* D10..D13 -> PB4..PB7 */
        *port = &PORTB;
        *bit  = (uint8_t)(pin - 6);
        return true;
    }
    if (pin >= 22 && pin <= 29) {         /* D22..D29 -> PA0..PA7 */
        *port = &PORTA;
        *bit  = (uint8_t)(pin - 22);
        return true;
    }
    if (pin >= 30 && pin <= 37) {         /* D30..D37 -> PC7..PC0, reversed */
        *port = &PORTC;
        *bit  = (uint8_t)(37 - pin);
        return true;
    }
    return false;
}

gpio_result_t gpio_set_output(uint8_t pin)
{
    volatile uint8_t *port;
    uint8_t bit;

    if (gpio_is_reserved(pin))            return GPIO_RESERVED;
    if (!pin_lookup(pin, &port, &bit))    return GPIO_UNKNOWN_PIN;

    volatile uint8_t *ddr = port - 1;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        *ddr |= (uint8_t)(1u << bit);
    }
    return GPIO_OK;
}

gpio_result_t gpio_set_input(uint8_t pin, bool pullup)
{
    volatile uint8_t *port;
    uint8_t bit;

    if (gpio_is_reserved(pin))            return GPIO_RESERVED;
    if (!pin_lookup(pin, &port, &bit))    return GPIO_UNKNOWN_PIN;

    volatile uint8_t *ddr = port - 1;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        *ddr &= (uint8_t)~(1u << bit);
        if (pullup) {
            *port |= (uint8_t)(1u << bit);
        } else {
            *port &= (uint8_t)~(1u << bit);
        }
    }
    return GPIO_OK;
}

gpio_result_t gpio_write(uint8_t pin, bool high)
{
    volatile uint8_t *port;
    uint8_t bit;

    if (gpio_is_reserved(pin))            return GPIO_RESERVED;
    if (!pin_lookup(pin, &port, &bit))    return GPIO_UNKNOWN_PIN;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (high) {
            *port |= (uint8_t)(1u << bit);
        } else {
            *port &= (uint8_t)~(1u << bit);
        }
    }
    return GPIO_OK;
}

gpio_result_t gpio_read(uint8_t pin, bool *value)
{
    volatile uint8_t *port;
    uint8_t bit;

    if (gpio_is_reserved(pin))            return GPIO_RESERVED;
    if (!pin_lookup(pin, &port, &bit))    return GPIO_UNKNOWN_PIN;

    /* A single register read needs no lock - it is already one
     * instruction and cannot be torn by a context switch. */
    volatile uint8_t *pinreg = port - 2;
    *value = (*pinreg & (uint8_t)(1u << bit)) != 0;
    return GPIO_OK;
}
