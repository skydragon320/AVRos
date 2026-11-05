/*
 * gpio.h - Pin access by Arduino pin number, without the Arduino core.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    GPIO_OK = 0,
    GPIO_UNKNOWN_PIN,     /* not in the table (see gpio.c)              */
    GPIO_RESERVED         /* the kernel owns it; see gpio_is_reserved() */
} gpio_result_t;

/* True for pins the kernel has claimed and will not hand out. */
bool gpio_is_reserved(uint8_t pin);

gpio_result_t gpio_set_output(uint8_t pin);
gpio_result_t gpio_set_input(uint8_t pin, bool pullup);
gpio_result_t gpio_write(uint8_t pin, bool high);
gpio_result_t gpio_read(uint8_t pin, bool *value);

#endif /* GPIO_H */
