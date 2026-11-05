/*
 * sched.c - Task table, round-robin scheduler, and the task-facing
 *           services (spawn, yield, sleep, suspend, resume).
 *
 * The policy here is deliberately simple: preemptive round robin with no
 * priorities. Every ready task gets one tick, in order, forever. That is
 * enough to demonstrate real concurrency, and it keeps the interesting
 * complexity in the context switch rather than in the queue discipline.
 *
 * Slot 0 of the task table is always the idle task. It is excluded from
 * the normal rotation and only runs when nothing else can, which keeps a
 * valid stack under the CPU when every real task is asleep.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <string.h>

#include "task.h"

/* ---- Kernel state ---------------------------------------------------- */

tcb_t   g_tasks[OS_MAX_TASKS];
uint8_t g_task_count;
uint8_t g_current_id;
tcb_t  *g_current_tcb;

volatile uint32_t g_ticks;

/* The idle task needs somewhere to keep its context. It makes no calls,
 * so a saved frame plus room for one interrupt is plenty. */
#define IDLE_STACK_BYTES 96
static uint8_t idle_stack[IDLE_STACK_BYTES];

/* How many bytes at the bottom of a stack are treated as the canary
 * region. If any of them lose their paint, the task wrote past the end. */
#define CANARY_BYTES 4

/* ---- Idle task ------------------------------------------------------- */

/*
 * Runs when every other task is asleep or suspended. It does nothing at
 * all on purpose: sleeping the CPU here would be the obvious improvement,
 * but SLEEP_MODE_IDLE interacts with the tick timer and is left for a
 * later revision rather than guessed at now.
 */
static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* spin */
    }
}

/* ---- Timer ----------------------------------------------------------- */

/*
 * Timer1 in CTC mode with a /64 prescaler. At 16 MHz that is a 250 kHz
 * count, so OCR1A = 249 gives a compare match every 1 ms.
 *
 * Timer1 is chosen because it is 16-bit (so the divisor lands exactly)
 * and because nothing else wants it - with no Arduino core running, every
 * peripheral on the chip is unclaimed.
 */
static void tick_timer_init(void)
{
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);  /* CTC, /64 */
    OCR1A  = (F_CPU / 64UL / OS_TICK_HZ) - 1UL;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);
}

/* ---- Scheduler ------------------------------------------------------- */

/*
 * Called from the timer ISR with the outgoing context already pushed.
 *
 * Interrupts are off for the whole of this function (the hardware cleared
 * the global flag on entry and the naked ISR never sets it), so the task
 * table can be walked without locking.
 */
void os_tick_schedule(void)
{
    g_ticks++;

    /* Wake anything whose deadline has passed. The subtraction and signed
     * compare are what make this survive the 32-bit tick counter wrapping
     * after ~49 days; comparing the two values directly would not. */
    for (uint8_t i = 1; i < g_task_count; i++) {
        if (g_tasks[i].state == OS_SLEEP &&
            (int32_t)(g_ticks - g_tasks[i].wake_at) >= 0) {
            g_tasks[i].state = OS_READY;
        }
    }

    os_pick_next();
}

/*
 * Pick the next task to run. Deliberately does not touch g_ticks: a
 * voluntary yield is not the passage of time, and conflating the two is
 * what broke the clock in the first version of this kernel.
 *
 * Interrupts are off at every call site - either the hardware cleared the
 * flag on entry to the tick ISR, or os_switch_now() cleared it explicitly
 * - so the task table needs no locking here.
 */
void os_pick_next(void)
{
    /* Round robin over the real tasks, starting after the current one so
     * nobody gets two slices in a row. */
    for (uint8_t n = 1; n < g_task_count; n++) {
        uint8_t id = g_current_id + n;
        if (id >= g_task_count) {
            id -= (g_task_count - 1);   /* wrap, skipping slot 0 */
        }
        if (id == 0) {
            continue;
        }
        if (g_tasks[id].state == OS_READY) {
            g_current_id  = id;
            g_current_tcb = &g_tasks[id];
            return;
        }
    }

    /* Still here: either the current task is the only runnable one, or
     * nothing is runnable at all. */
    if (g_current_id != 0 && g_tasks[g_current_id].state == OS_READY) {
        return;                          /* keep running the current task */
    }

    g_current_id  = 0;
    g_current_tcb = &g_tasks[0];
}

/* ---- Task creation --------------------------------------------------- */

/*
 * The idle task has to occupy slot 0, because the scheduler's rotation
 * skips index 0 and falls back to it. Creating it lazily on the first
 * spawn - rather than in os_start() - means the application cannot get
 * the ordering wrong by spawning its own tasks first.
 */
static void ensure_idle_task(void)
{
    if (g_task_count != 0) {
        return;
    }
    tcb_t *t = &g_tasks[0];

    t->stack_base = idle_stack;
    t->stack_size = sizeof(idle_stack);
    t->state      = OS_READY;
    t->wake_at    = 0;
    strncpy(t->name, "idle", OS_NAME_LEN);
    t->name[OS_NAME_LEN] = '\0';

    os_stack_init(t, idle_task, NULL);
    g_task_count = 1;
}

int8_t os_spawn(os_task_fn fn, void *arg,
                uint8_t *stack, uint16_t stack_size,
                const char *name)
{
    ensure_idle_task();

    if (g_task_count >= OS_MAX_TASKS) {
        return -1;
    }
    /* Refuse a stack that cannot even hold a saved context plus a little
     * working room. Failing loudly here beats a mysterious reset later. */
    if (stack == NULL || stack_size < OS_CONTEXT_BYTES + 32) {
        return -1;
    }

    tcb_t *t = &g_tasks[g_task_count];

    t->stack_base = stack;
    t->stack_size = stack_size;
    t->state      = OS_READY;
    t->wake_at    = 0;

    strncpy(t->name, name ? name : "?", OS_NAME_LEN);
    t->name[OS_NAME_LEN] = '\0';

    os_stack_init(t, fn, arg);

    return (int8_t)g_task_count++;
}

void os_start(void)
{
    /* Covers the degenerate case of starting with no application tasks
     * at all; normally the first os_spawn() has already done this. */
    ensure_idle_task();

    g_current_id  = 0;
    g_current_tcb = &g_tasks[0];

    tick_timer_init();
    os_dispatch_first();          /* never returns */
}

/* ---- Task services --------------------------------------------------- */

/*
 * Give up the rest of this time slice.
 *
 * Switches immediately and unconditionally. The timer is left alone, so
 * yielding has no effect on the system clock and a tight yield loop - as
 * in uart_getc() - cannot starve the tick.
 */
void os_yield(void)
{
    os_switch_now();
}

void os_sleep(uint16_t ms)
{
    if (ms == 0) {
        os_yield();
        return;
    }

    /* Marking the task asleep and switching away have to be one
     * indivisible step. If a tick landed in between, the scheduler would
     * see OS_SLEEP and switch us out, and we would come back later only
     * to switch again for no reason. os_switch_now() ends in reti, which
     * restores the interrupt flag, so this cli needs no matching sei. */
    cli();
    tcb_t *t = &g_tasks[g_current_id];
    t->wake_at = g_ticks + ms;            /* OS_TICK_HZ is 1000: 1 tick = 1 ms */
    t->state   = OS_SLEEP;
    os_switch_now();
}

uint32_t os_millis(void)
{
    uint32_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        v = g_ticks;
    }
    return v;
}

uint8_t os_self(void)
{
    return g_current_id;
}

/* ---- Introspection --------------------------------------------------- */

uint8_t os_task_count(void)
{
    return g_task_count;
}

const char *os_task_name(uint8_t id)
{
    return (id < g_task_count) ? g_tasks[id].name : "?";
}

os_state_t os_task_state(uint8_t id)
{
    return (id < g_task_count) ? g_tasks[id].state : OS_DEAD;
}

uint16_t os_stack_size(uint8_t id)
{
    return (id < g_task_count) ? g_tasks[id].stack_size : 0;
}

uint16_t os_stack_used(uint8_t id)
{
    if (id >= g_task_count) {
        return 0;
    }
    const tcb_t *t = &g_tasks[id];

    /* Walk up from the bottom until the paint runs out. Whatever is above
     * that point has been touched at some moment since boot, so it is a
     * true high-water mark rather than a snapshot of current depth. */
    uint16_t untouched = 0;
    while (untouched < t->stack_size &&
           t->stack_base[untouched] == OS_STACK_PAINT) {
        untouched++;
    }
    return t->stack_size - untouched;
}

bool os_stack_overflowed(uint8_t id)
{
    if (id >= g_task_count) {
        return false;
    }
    const tcb_t *t = &g_tasks[id];
    for (uint8_t i = 0; i < CANARY_BYTES; i++) {
        if (t->stack_base[i] != OS_STACK_PAINT) {
            return true;
        }
    }
    return false;
}

bool os_suspend(uint8_t id)
{
    if (id == 0 || id >= g_task_count) {
        return false;            /* idle may not be suspended */
    }
    uint8_t saved = SREG;
    cli();
    g_tasks[id].state = OS_SUSPENDED;
    if (id == g_current_id) {
        os_switch_now();         /* we just parked ourselves; reti restores I */
    } else {
        SREG = saved;
    }
    return true;
}

bool os_resume(uint8_t id)
{
    if (id == 0 || id >= g_task_count) {
        return false;
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (g_tasks[id].state == OS_SUSPENDED) {
            g_tasks[id].state = OS_READY;
        }
    }
    return true;
}
