/*
 * os.h - Public API for a small preemptive multitasking kernel (ATmega2560)
 *
 * This is everything a task needs to see. The internals (task control
 * blocks, the context switch, the scheduler) live in task.h / sched.c /
 * context.c and are deliberately not exposed here.
 *
 * Model: several tasks share one core and one address space. There is no
 * MMU on this chip, so there is no memory protection and no isolation -
 * a task that overflows its stack will quietly corrupt a neighbour. The
 * kernel detects that after the fact with stack canaries (see os_stack_used)
 * but cannot prevent it.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#ifndef OS_H
#define OS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* NULL - the API traffics in pointers */

/* ---- Configuration -------------------------------------------------- */

#define OS_MAX_TASKS      8      /* including the idle task (slot 0)      */
#define OS_TICK_HZ        1000   /* scheduler tick rate, Hz               */
#define OS_NAME_LEN       8      /* task name, not counting the NUL       */

/* Bytes of each task stack consumed by a saved context. See context.c.
 * 3 (program counter) + 32 (r0-r31) + 1 (SREG) + 1 (RAMPZ) + 1 (EIND). */
#define OS_CONTEXT_BYTES  38

/* Unused stack is painted with this value so os_stack_used() can find the
 * high-water mark by counting how much paint is left. */
#define OS_STACK_PAINT    0xA5

/* ---- Task lifecycle -------------------------------------------------- */

typedef void (*os_task_fn)(void *arg);

/*
 * Create a task. The caller owns the stack memory, which makes every
 * task's RAM cost visible at the call site instead of hidden in a kernel
 * pool - on a chip with 8 KB of SRAM that visibility is worth having.
 *
 * The stack must be at least OS_CONTEXT_BYTES + whatever the task itself
 * needs, plus headroom for any interrupt that fires while the task is
 * running (interrupt handlers borrow the stack of whichever task they
 * interrupt). 128 bytes is a reasonable floor; 256 if the task formats
 * strings.
 *
 * Returns the task id, or -1 if the task table is full or the stack is
 * too small. Safe to call before os_start().
 */
int8_t os_spawn(os_task_fn fn, void *arg,
                uint8_t *stack, uint16_t stack_size,
                const char *name);

/* Start scheduling. Never returns. */
void os_start(void) __attribute__((noreturn));

/* ---- Task services (call these from inside a task) ------------------- */

/* Give up the rest of this time slice. */
void os_yield(void);

/* Block this task for at least ms milliseconds. Unlike a delay loop, the
 * CPU is handed to another task for the duration. */
void os_sleep(uint16_t ms);

/* Milliseconds since os_start(). Wraps after ~49 days. */
uint32_t os_millis(void);

/* Id of the task that is currently running. */
uint8_t os_self(void);

/* ---- Introspection (used by the shell) ------------------------------- */

typedef enum {
    OS_READY = 0,   /* runnable now                                      */
    OS_SLEEP,       /* waiting for a wake time                           */
    OS_SUSPENDED,   /* parked by the user until resumed                  */
    OS_DEAD         /* returned from its entry function                  */
} os_state_t;

uint8_t     os_task_count(void);
const char *os_task_name(uint8_t id);
os_state_t  os_task_state(uint8_t id);
uint16_t    os_stack_size(uint8_t id);

/* High-water mark: the most stack this task has ever used, in bytes.
 * Found by counting how many OS_STACK_PAINT bytes survive at the bottom
 * of the stack, so it is a measurement, not an estimate. */
uint16_t    os_stack_used(uint8_t id);

/* True if a task has written past the bottom of its stack. Once this is
 * true the damage is already done; it is a diagnostic, not a guard. */
bool        os_stack_overflowed(uint8_t id);

bool os_suspend(uint8_t id);
bool os_resume(uint8_t id);

#endif /* OS_H */
