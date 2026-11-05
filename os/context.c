/*
 * context.c - The context switch. This is the heart of the kernel and the
 *             one part that cannot be written in C.
 *
 * WHY ASSEMBLY
 *
 * Switching tasks means saving every register the outgoing task was using
 * and restoring every register the incoming task was using. C has no way
 * to name "all 32 general purpose registers", and no way to swap the stack
 * pointer out from under itself without the compiler's prologue code
 * disagreeing about where the stack is. So this is hand-written AVR
 * assembly, and the rest of the kernel is C on top of it.
 *
 * HOW A SWITCH HAPPENS
 *
 * Timer1 raises a compare-match interrupt every millisecond. The hardware
 * pushes the program counter and clears the global interrupt flag, then
 * jumps here. We:
 *
 *   1. push r0, SREG, r1..r31, RAMPZ and EIND onto the CURRENT task's stack
 *   2. store the stack pointer into that task's TCB
 *   3. call the C scheduler, which repoints g_current_tcb at another task
 *   4. load the stack pointer out of the NEW task's TCB
 *   5. pop everything back off the NEW task's stack
 *   6. reti
 *
 * Because step 4 changed the stack pointer, the `reti` in step 6 pops a
 * return address belonging to a different task, and execution resumes
 * wherever that task was last interrupted. Nothing about the switch is
 * visible to either task.
 *
 * ATmega2560 SPECIFICS
 *
 * This chip has 256 KB of flash, which means a 3-byte program counter
 * rather than the 2-byte one on smaller AVRs. It also has RAMPZ (for
 * far flash reads) and EIND (for far indirect calls). All three must be
 * part of the saved context, which is why a frame here is 38 bytes and
 * only 35 on an ATmega328P.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>

#include "task.h"

/* I/O-space addresses for the two extended registers. avr-libc exposes
 * RAMPZ as __RAMPZ__ for inline asm; EIND has no such alias, so it is
 * given numerically. Both are inside the 0x00-0x3F range that `in` and
 * `out` can reach. */
#define EIND_IO   "0x3C"
#define RAMPZ_IO  "0x3B"

/*
 * Push order: r0, SREG, r1, r2 .. r31, RAMPZ, EIND.
 *
 * r0 goes first because we immediately need a scratch register to read
 * SREG into. r1 is zeroed right after it is saved because avr-gcc's ABI
 * requires r1 to hold zero at all times in compiled code, and we are
 * about to call a C function.
 *
 * The last four instructions write the resulting stack pointer into the
 * current TCB. X (r27:r26) holds the TCB address; `sp` is at offset 0 of
 * that struct, which task.h documents as a hard requirement.
 */
#define SAVE_CONTEXT()                       \
    "push r0                           \n\t" \
    "in   r0, __SREG__                 \n\t" \
    "push r0                           \n\t" \
    "push r1                           \n\t" \
    "clr  r1                           \n\t" \
    "push r2                           \n\t" \
    "push r3                           \n\t" \
    "push r4                           \n\t" \
    "push r5                           \n\t" \
    "push r6                           \n\t" \
    "push r7                           \n\t" \
    "push r8                           \n\t" \
    "push r9                           \n\t" \
    "push r10                          \n\t" \
    "push r11                          \n\t" \
    "push r12                          \n\t" \
    "push r13                          \n\t" \
    "push r14                          \n\t" \
    "push r15                          \n\t" \
    "push r16                          \n\t" \
    "push r17                          \n\t" \
    "push r18                          \n\t" \
    "push r19                          \n\t" \
    "push r20                          \n\t" \
    "push r21                          \n\t" \
    "push r22                          \n\t" \
    "push r23                          \n\t" \
    "push r24                          \n\t" \
    "push r25                          \n\t" \
    "push r26                          \n\t" \
    "push r27                          \n\t" \
    "push r28                          \n\t" \
    "push r29                          \n\t" \
    "push r30                          \n\t" \
    "push r31                          \n\t" \
    "in   r0, " RAMPZ_IO             "\n\t" \
    "push r0                           \n\t" \
    "in   r0, " EIND_IO              "\n\t" \
    "push r0                           \n\t" \
    "lds  r26, g_current_tcb           \n\t" \
    "lds  r27, g_current_tcb+1         \n\t" \
    "in   r0, __SP_L__                 \n\t" \
    "st   X+, r0                       \n\t" \
    "in   r0, __SP_H__                 \n\t" \
    "st   X+, r0                       \n\t"

/*
 * Pop order is the exact mirror: EIND, RAMPZ, r31 .. r2, r1, SREG, r0.
 *
 * The stack pointer is reloaded from the TCB first, so every pop after
 * that reads from the incoming task's stack. Writing SPL before SPH is
 * safe here because interrupts are off for the whole sequence and nothing
 * touches the stack in between.
 *
 * SREG is restored with its interrupt flag clear (that is how it was
 * saved, inside an interrupt). The `reti` that follows sets the flag, so
 * the task resumes with interrupts enabled.
 */
#define RESTORE_CONTEXT()                    \
    "lds  r26, g_current_tcb           \n\t" \
    "lds  r27, g_current_tcb+1         \n\t" \
    "ld   r0, X+                       \n\t" \
    "out  __SP_L__, r0                 \n\t" \
    "ld   r0, X+                       \n\t" \
    "out  __SP_H__, r0                 \n\t" \
    "pop  r0                           \n\t" \
    "out  " EIND_IO ", r0              \n\t" \
    "pop  r0                           \n\t" \
    "out  " RAMPZ_IO ", r0             \n\t" \
    "pop  r31                          \n\t" \
    "pop  r30                          \n\t" \
    "pop  r29                          \n\t" \
    "pop  r28                          \n\t" \
    "pop  r27                          \n\t" \
    "pop  r26                          \n\t" \
    "pop  r25                          \n\t" \
    "pop  r24                          \n\t" \
    "pop  r23                          \n\t" \
    "pop  r22                          \n\t" \
    "pop  r21                          \n\t" \
    "pop  r20                          \n\t" \
    "pop  r19                          \n\t" \
    "pop  r18                          \n\t" \
    "pop  r17                          \n\t" \
    "pop  r16                          \n\t" \
    "pop  r15                          \n\t" \
    "pop  r14                          \n\t" \
    "pop  r13                          \n\t" \
    "pop  r12                          \n\t" \
    "pop  r11                          \n\t" \
    "pop  r10                          \n\t" \
    "pop  r9                           \n\t" \
    "pop  r8                           \n\t" \
    "pop  r7                           \n\t" \
    "pop  r6                           \n\t" \
    "pop  r5                           \n\t" \
    "pop  r4                           \n\t" \
    "pop  r3                           \n\t" \
    "pop  r2                           \n\t" \
    "pop  r1                           \n\t" \
    "pop  r0                           \n\t" \
    "out  __SREG__, r0                 \n\t" \
    "pop  r0                           \n\t"

/*
 * The scheduler tick.
 *
 * ISR_NAKED suppresses the compiler's prologue and epilogue, which is
 * essential: the default prologue would push registers before our save
 * sequence runs, putting bytes on the stack that the restore sequence
 * does not know about.
 *
 * The whole body is one asm volatile block on purpose. Splitting it into
 * several statements would let the compiler schedule its own code in
 * between the save and the restore, where there is no valid stack frame
 * to work with.
 */
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
    __asm__ __volatile__ (
        SAVE_CONTEXT()
        "call os_tick_schedule             \n\t"
        RESTORE_CONTEXT()
        "reti                              \n\t"
        ::: "memory"
    );
}

/*
 * Switch tasks on demand, outside of any interrupt.
 *
 * This exists because the first version of the kernel did not have it,
 * and that was a mistake worth recording. os_yield() used to work by
 * winding Timer1 forward so the scheduler interrupt would fire early.
 * Two things were wrong with that. It could not guarantee a switch - if
 * the match did not land, os_yield() simply returned and the caller kept
 * running, which quietly broke os_sleep(). And a tight yield loop reset
 * the counter at roughly the same rate the counter advanced, starving
 * the compare match and dragging the system clock down with it.
 *
 * The real fix is for a voluntary switch to be a switch, not a request
 * for one. That turns out to be nearly free here: `call` pushes a 3-byte
 * return address on this chip, which is exactly the frame an interrupt
 * leaves behind, so the save and restore sequences above work unchanged.
 * Ending in `reti` rather than `ret` both returns and re-enables
 * interrupts, so a context saved by the timer ISR and a context saved by
 * a yield are fully interchangeable.
 *
 * The leading `cli` is load-bearing: without it the timer interrupt can
 * land partway through the save sequence, stack a second context on top
 * of a half-written one, and switch away leaving both corrupt.
 */
void os_switch_now(void)
{
    __asm__ __volatile__ (
        "cli                               \n\t"
        SAVE_CONTEXT()
        "call os_pick_next                 \n\t"
        RESTORE_CONTEXT()
        "reti                              \n\t"
        ::: "memory"
    );
}

/*
 * Hand control to the first task.
 *
 * There is no outgoing context to save - the startup code's stack is
 * abandoned here and never used again. We just restore the first task's
 * fabricated context and reti into it.
 */
void os_dispatch_first(void)
{
    __asm__ __volatile__ (
        RESTORE_CONTEXT()
        "reti                              \n\t"
        ::: "memory"
    );
    __builtin_unreachable();
}

/*
 * Fabricate a saved context on a brand new task's stack.
 *
 * The bytes written here are laid out in exactly the order SAVE_CONTEXT
 * pushes them, so that the first RESTORE_CONTEXT pops a coherent machine
 * state and the trailing `reti` jumps to the task's entry function. If
 * this ordering and the macro above ever drift apart, tasks will jump to
 * garbage addresses - keep them side by side.
 *
 * Note on the return address: on AVR a function pointer holds a WORD
 * address, not a byte address, which is exactly what the program counter
 * wants. No shifting is needed.
 */
void os_stack_init(tcb_t *t, os_task_fn fn, void *arg)
{
    uint16_t entry = (uint16_t)(void *)fn;
    uint16_t a     = (uint16_t)arg;

    /* Paint the whole stack so os_stack_used() can measure the high-water
     * mark later by counting surviving paint. */
    memset(t->stack_base, OS_STACK_PAINT, t->stack_size);

    /* Highest byte of the buffer; the stack grows downward from here. */
    uint8_t *p = t->stack_base + t->stack_size - 1;

    /* Return address, 3 bytes. Pushed low byte first by the hardware, so
     * the low byte sits at the highest address. */
    *p-- = (uint8_t)(entry & 0xFF);
    *p-- = (uint8_t)(entry >> 8);
    *p-- = 0x00;                      /* PC bits 23:16 - our code is low */

    *p-- = 0x00;                      /* r0                              */
    *p-- = 0x00;                      /* SREG - reti will set the I flag */
    *p-- = 0x00;                      /* r1 - the ABI's permanent zero   */

    /* r2 through r31. The AVR calling convention passes the first pointer
     * argument in r25:r24, so seeding those two is what makes the task's
     * `arg` parameter arrive correctly. */
    for (uint8_t r = 2; r <= 31; r++) {
        if (r == 24)      *p-- = (uint8_t)(a & 0xFF);
        else if (r == 25) *p-- = (uint8_t)(a >> 8);
        else              *p-- = 0x00;
    }

    *p-- = 0x00;                      /* RAMPZ */
    *p-- = 0x00;                      /* EIND  */

    /* SP points at the next free byte, which is one below the last write. */
    t->sp = (uint16_t)p;
}
