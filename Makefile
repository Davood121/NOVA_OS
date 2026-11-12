CC      = gcc
AS      = as
LD      = ld
OBJCOPY = objcopy

CFLAGS  = -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -nostdlib -O2 -Wall -Wextra
ASFLAGS = --32
BOOTLD  = -m elf_i386 -T src/boot/boot.ld --oformat binary
KRNL_LD = -m elf_i386 -T src/kernel/kernel.ld -N

KERNEL_MAX = 31744

BOOT_SRCS  = src/boot/boot.S
KRNL_SRCS  = src/kernel/entry.S \
             src/kernel/isr.S \
             src/kernel/main.c \
             src/kernel/console.c \
             src/kernel/kbd.c \
             src/kernel/idt.c \
             src/kernel/rtc.c \
             src/kernel/mem.c \
             src/kernel/pit.c \
             src/kernel/cpu.c \
             src/kernel/shell.c \
             src/kernel/util.c

KRNL_OBJS = $(patsubst src/%,build/%,$(addsuffix .o,$(basename $(KRNL_SRCS))))

all: grid-os.img

build:
	mkdir -p build

build/boot.o: $(BOOT_SRCS) | build
	$(AS) $(ASFLAGS) -o $@ $<

build/boot.bin: build/boot.o
	$(LD) $(BOOTLD) -o $@ $<
	@sz=$$(stat -c%s $@); \
	if [ $$sz -ne 512 ]; then echo "boot.bin must be 512 bytes, got $$sz"; exit 1; fi

build/%.o: src/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S | build
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

build/kernel.elf: $(KRNL_OBJS) src/kernel/kernel.ld | build
	$(LD) $(KRNL_LD) -o $@ $(KRNL_OBJS)

build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(stat -c%s $@); \
	echo "kernel.bin: $$sz bytes"; \
	if [ $$sz -gt $(KERNEL_MAX) ]; then echo "kernel exceeds $(KERNEL_MAX) byte limit"; exit 1; fi

grid-os.img: build/boot.bin build/kernel.bin
	truncate -s 4M $@
	dd if=build/boot.bin   of=$@ conv=notrunc
	dd if=build/kernel.bin of=$@ seek=1 conv=notrunc bs=512

clean:
	rm -rf build grid-os.img

.PHONY: all clean
