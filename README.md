# A small preemptive kernel for the Arduino Mega 2560

CS 301 semester project — Skylar J. Sabin

A multitasking kernel that runs bare-metal on an ATmega2560: no Arduino
core, no framework, no libraries beyond avr-libc's register headers. The
context switch is hand-written AVR assembly; everything on top of it is C.

This is the proof-of-concept stage. The scheduler, the task services and
the shell work; the features listed under *Not built yet* are the ones
still ahead.

## What it does

- **Preemptive round-robin scheduling.** A Timer1 compare match every
  millisecond interrupts whatever is running, saves its registers, and
  resumes a different task.
- **Task services.** `os_spawn`, `os_sleep`, `os_yield`, `os_suspend`,
  `os_resume`. A sleeping task costs no CPU — it leaves the rotation
  entirely until its wake time.
- **A serial shell, which is itself a task.** This is the part worth
  looking at. `ps` answers while the other tasks keep running, because
  the shell has no special standing — it is scheduled like everything
  else, and it blocks by yielding rather than by spinning.
- **Measured stack usage.** Every stack is painted at creation and `mem`
  reports the high-water mark by counting surviving paint, so the numbers
  are observations rather than estimates.
- **Pin control from the prompt.** `pin 22 out`, `pin 22 high`, `read 22`
  — toggling hardware with no code and no recompile.

## Hardware

An Arduino Mega 2560 and a USB cable. Nothing else is required; the demo
uses the on-board LED on pin 13.

The stock bootloader is left in place, so uploads work over USB exactly
as they would for a sketch, and the board can be returned to normal
Arduino use at any time by uploading any sketch over the top.

## Build and run

```
make            # build
make size       # check flash and SRAM
make flash      # upload   (PORT=/dev/ttyACM0 by default)
make monitor    # open the console at 115200 baud
```

Requires `avr-gcc`, `avr-libc` and `avrdude`. On Debian or Ubuntu:
`sudo apt install gcc-avr avr-libc avrdude`.

Then, at the prompt:

```
> ps
id name      state
0  idle      ready
1  blink     sleep
2  heart     sleep
3  shell     ready  <- you

> suspend 1          # the LED stops
> resume 1           # and starts again
> pin 22 out
> pin 22 high
> mem
```

The LED keeps blinking the whole time you are typing. That is the point
of the exercise.

## How the context switch works

`os/context.c` is the file to read first. The short version:

1. Timer1 raises a compare match; the hardware pushes the program counter
   and clears the global interrupt flag.
2. The ISR — declared `ISR_NAKED`, so the compiler adds no prologue —
   pushes r0, SREG, r1 through r31, RAMPZ and EIND onto the running
   task's stack, then stores the stack pointer into that task's control
   block.
3. It calls the C scheduler, which repoints `g_current_tcb` at whichever
   task should run next.
4. It loads the stack pointer back out of the *new* task's control block
   and pops everything off that stack.
5. `reti` returns to an address belonging to a different task.

A saved context is 38 bytes: 3 for the program counter, 32 for the
register file, and one each for SREG, RAMPZ and EIND.

The three-byte program counter is the Mega-specific wrinkle. Smaller AVRs
have a two-byte PC and no RAMPZ or EIND, so the same kernel on an
ATmega328P would use 35-byte frames. Code written against one will not
run on the other without adjusting both the push sequence and the
fabricated startup frame in `os_stack_init`, which is why those two live
next to each other in the same file.

## Known limitations

These are deliberate, not undiscovered.

- **No memory protection, at all.** There is no MMU on this chip. A task
  that overflows its stack corrupts a neighbour's memory and nothing
  stops it. The canaries in `mem` detect the damage after the fact; they
  cannot prevent it. The reserved-pin check in the shell has the same
  character — it stops a typo at the prompt, and does nothing about a
  task that writes `PORTE` directly.
- **Round robin only.** No priorities, no aging. Every ready task gets
  one tick in turn.
- **No inter-task communication yet.** Tasks share globals and nothing
  guards them.
- **The pin table is partial.** It covers D0–D1, D10–D13, D22–D29 and
  D30–D37. The remaining Mega pins are mechanical to add, but typing 70
  port/bit pairs by hand is how transposition errors get in; the plan is
  to generate the table from the Arduino variant file rather than copy it
  by eye.

## What went wrong on the first run

Worth recording, because it was a design error rather than a typo.

The first version had no voluntary switch path. `os_yield()` instead wound
Timer1 forward to one count below its compare value, so the ordinary
scheduler interrupt would fire early and do the switch. One mechanism,
less assembly — it looked like the simpler choice.

It failed in two ways at once. A yield could not *guarantee* a switch: if
the compare match did not land, `os_yield()` just returned and the caller
carried on, which meant `os_sleep()` set a task's state to asleep and then
kept running it. Tasks reported as sleeping in `ps` while they were plainly
executing. And the shell's input loop — `uart_try_getc` plus `os_yield`,
about 4.4 µs per pass — reset the counter at almost exactly the 4 µs the
counter needed to reach its compare value, starving the match. The system
clock dropped to roughly 1.2 ticks per second instead of 1000.

Both symptoms came from the same root cause: the switch path and the clock
were the same mechanism, so using one disturbed the other.

The fix is `os_switch_now()` in `context.c` — a voluntary switch that saves
and restores a context itself rather than asking a timer to arrange it. It
costs almost nothing in code because `call` pushes a 3-byte return address
on this chip, which is the same frame an interrupt leaves, so the existing
save and restore sequences work unchanged. `g_ticks` now advances only on
real timer expiries.

The lesson generalises: *a request for something to happen is not the same
as the thing happening*, and a kernel primitive that only usually works is
worse than one that is obviously missing.

## Not built yet

- Semaphores and a message queue
- A `kill` command and task teardown
- Replacing the stock bootloader with a custom one, with multiple
  application slots selectable at boot

## Layout

```
Makefile          build, upload, fuse-read and disassembly targets
main.c            demo tasks and startup
os/os.h           the API a task sees
os/task.h         task control block (kernel-internal)
os/context.c      the context switch — hand-written assembly
os/sched.c        task table, scheduler, sleep/yield/suspend
os/uart.c         interrupt-driven console on UART0
os/gpio.c         pin access by Arduino pin number
os/shell.c        the command shell, running as a task
```

## Sources and prior work

FreeRTOS has a long-standing AVR port, and its approach to saving the
register file in a naked ISR is the conventional solution to this problem
— the structure here follows the same idea, which is the natural shape
for the instruction set rather than anything original. The context switch,
scheduler, stack initialisation and shell in this repository are written
from scratch; no FreeRTOS source is included or adapted.

ArduinOS (ContentsViewer) takes the opposite approach of extracting a
subset of FreeRTOS for teaching purposes. An unrelated project of the same
name (lrusso) turns a Mega into a thin-client terminal, and is application
code rather than a kernel.
