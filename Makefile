#
# Makefile - bare-metal build for the ATmega2560 (Arduino Mega 2560)
#
# No Arduino core, no framework. Just avr-gcc, avr-libc headers for the
# register definitions, and avrdude to push the result over USB.
#
#   make              build avros.hex
#   make size         report flash and SRAM usage
#   make flash        upload via the stock bootloader (PORT=... to override)
#   make readfuses    print the current fuse bytes - do this before
#                     touching fuses, and keep the output somewhere safe
#   make disasm       annotated disassembly, handy for checking that the
#                     context switch came out as intended
#   make clean
#
# Author: Skylar J. Sabin - CS 301 semester project
#

TARGET     = avros
MCU        = atmega2560
F_CPU      = 16000000UL

# Serial port the Mega enumerates as. Linux: /dev/ttyACM0. macOS:
# /dev/cu.usbmodem*. Windows under MSYS: COM3 or similar.
PORT      ?= /dev/ttyACM0

# "wiring" is the stk500v2 variant the Mega's stock bootloader speaks.
PROGRAMMER = wiring
BAUD       = 115200

SRC = main.c          \
      os/context.c    \
      os/sched.c      \
      os/uart.c       \
      os/gpio.c       \
      os/shell.c

OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CC      = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE    = avr-size

# -Os matters here: this has to fit alongside whatever else ends up on
# the chip, and smaller code also means fewer flash wait states.
#
# Link-time optimisation is deliberately NOT enabled. The context switch
# reaches g_current_tcb and os_tick_schedule by symbol name from inline
# assembly, and LTO is free to rename or discard symbols it believes
# nothing references.
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) \
          -Os -std=gnu11 \
          -Wall -Wextra -Wundef -Wshadow \
          -funsigned-char -funsigned-bitfields -fshort-enums \
          -ffunction-sections -fdata-sections \
          -MMD -MP

LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map,$(TARGET).map

.PHONY: all size flash readfuses disasm monitor clean

all: $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

size: $(TARGET).elf
	@$(SIZE) --format=avr --mcu=$(MCU) $<

flash: $(TARGET).hex
	avrdude -p m2560 -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -D \
	        -U flash:w:$(TARGET).hex:i

# Read-only. Run this and save the output before changing any fuse.
# The high fuse is where BOOTSZ and BOOTRST live; the low fuse holds the
# clock source bits, and setting those wrong leaves the chip unable to
# answer even an ISP programmer.
readfuses:
	avrdude -p m2560 -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) \
	        -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

disasm: $(TARGET).elf
	$(OBJDUMP) -d -S $< > $(TARGET).lst
	@echo "wrote $(TARGET).lst"

# Minimal serial console. screen is usually already installed; picocom
# or your editor's monitor works just as well.
monitor:
	screen $(PORT) $(BAUD)

clean:
	rm -f $(OBJ) $(DEP) $(TARGET).elf $(TARGET).hex $(TARGET).map $(TARGET).lst

-include $(DEP)
