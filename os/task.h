/*
 * task.h - Task control block, shared between the scheduler and the
 *          context switch. Kernel-internal; tasks include os.h instead.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "os.h"

/*
 * CRITICAL LAYOUT REQUIREMENT
 *
 * `sp` must be the FIRST member of this struct, at offset 0.
 *
 * The context switch in context.c loads the address of the current TCB
 * into the X register and then does `st X+, r0` twice to store the saved
 * stack pointer. That assembly assumes the two bytes at offset 0 and 1
 * are the low and high halves of `sp`. Adding a member above `sp`, or
 * reordering, will silently corrupt the stack pointer on every switch -
 * which presents as the board resetting with no error message.
 */
typedef struct {
    uint16_t    sp;             /* saved stack pointer - MUST stay first */

    uint8_t    *stack_base;     /* lowest address of the stack buffer    */
    uint16_t    stack_size;

    os_state_t  state;
    uint32_t    wake_at;        /* tick to wake on, when state == OS_SLEEP */

    char        name[OS_NAME_LEN + 1];
} tcb_t;

/* The task table. Slot 0 is always the idle task. */
extern tcb_t   g_tasks[OS_MAX_TASKS];
extern uint8_t g_task_count;
extern uint8_t g_current_id;

/*
 * Pointer to the TCB of the running task.
 *
 * The context switch reads and writes this symbol directly from assembly
 * (`lds r26, g_current_tcb`), so it must stay a plain global with external
 * linkage and must not be optimised away or renamed.
 */
extern tcb_t  *g_current_tcb;

/* Monotonic tick counter, incremented once per timer interrupt. */
extern volatile uint32_t g_ticks;

/* Called from the timer ISR with the outgoing context already saved.
 * Advances time, wakes sleepers, then picks the next task. This is the
 * only place g_ticks moves, so the system clock depends on real timer
 * expiries and nothing else. */
void os_tick_schedule(void) __attribute__((used));

/* Choose the next task without touching the clock. Called from the tick
 * and from a voluntary switch. */
void os_pick_next(void) __attribute__((used));

/* Perform a context switch immediately, from ordinary task context rather
 * than from an interrupt. Defined in context.c. */
void os_switch_now(void) __attribute__((naked));

/* Build a fake saved context on a new task's stack so that the first
 * context restore "returns" into its entry function. Defined in context.c
 * because it has to mirror the push order of the switch exactly. */
void os_stack_init(tcb_t *t, os_task_fn fn, void *arg);

/* Hand control to the first task. Never returns. Defined in context.c. */
void os_dispatch_first(void) __attribute__((naked, noreturn));

#endif /* TASK_H */
