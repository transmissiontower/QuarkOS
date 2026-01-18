# ==================================================
# Toolchain
# ==================================================
CROSS ?= i686-elf-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
AS      = nasm
MCOPY   = mcopy
GRUB_MKRESCUE = $(shell if which grub2-mkrescue > /dev/null 2>&1; then echo grub2-mkrescue; else echo grub-mkrescue; fi)

# ==================================================
# Flags
# ==================================================
CFLAGS = -ffreestanding -Wall -Wextra -O2 -g -m32 -Isrc
USER_CFLAGS = -ffreestanding -Wall -Wextra -O2 -m32 -Isrc
FONT_ATLAS = Asm/boot/font_atlas.bin

LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# ==================================================
# Files
# ==================================================
KERNEL_C = \
	src/kernel/kernel.c \
	src/video/vga.c \
	src/arch/x86/gdt.c \
	src/util/util.c \
	src/arch/x86/idt.c \
	src/drivers/timer.c \
	src/drivers/ps2/keyboard.c \
	src/mm/memory.c \
	src/mm/kmalloc.c \
	src/drivers/GUI/framebuffer.c \
	src/video/font.c \
	src/drivers/ps2/mouse.c \
	src/video/window.c \
	src/gfx/events.c \
	src/arch/x86/fpu.c \
	src/lib/math.c \
	src/lib/stdlib.c \
	src/video/texture.c \
	src/video/compositor.c \
	src/gfx/bmp.c \
	src/gfx/font_atlas.c \
	src/drivers/rtc.c \
	src/kernel/scheduler.c \
	src/kernel/syscalls.c \
	src/fs/fat.c \
	src/drivers/ata.c

KERNEL_OBJ = $(KERNEL_C:.c=.o) \
	src/kernel/process.o \
	boot.o gdts.o idts.o

USER_LIB_OBJ = \
	src/user/lib/crt0.o \
	src/user/lib/libapi.o

USER_TEST_OBJ = $(USER_LIB_OBJ) src/user/test.o
USER_SHELL_OBJ = $(USER_LIB_OBJ) src/user/shell.o
USER_NOTEPAD_OBJ = $(USER_LIB_OBJ) src/user/notepad.o

# ==================================================
# Default target
# ==================================================
all: image

# ==================================================
# Kernel
# ==================================================
kernel: $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -o kernel $(KERNEL_OBJ)

# ==================================================
# Compile rules
# ==================================================
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

src/kernel/process.o: src/kernel/process.s
	$(AS) -f elf32 $< -o $@

boot.o: src/boot/boot.s
	$(AS) -f elf32 $< -o $@

gdts.o: src/arch/x86/gdt.s
	$(AS) -f elf32 $< -o $@

idts.o: src/arch/x86/idt.s
	$(AS) -f elf32 $< -o $@

$(FONT_ATLAS): tools/font_baker.py
	mkdir -p Asm/boot
	python3 tools/font_baker.py

# ==================================================
# Userland
# ==================================================
src/user/lib/crt0.o: src/user/lib/crt0.s
	$(AS) -f elf32 $< -o $@

src/user/lib/libapi.o: src/user/lib/libapi.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/test.o: src/user/test.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/shell.o: src/user/shell.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/notepad.o: src/user/notepad.c  
	$(CC) $(USER_CFLAGS) -c $< -o $@

user_bin: $(USER_TEST_OBJ) $(USER_SHELL_OBJ) $(USER_NOTEPAD_OBJ)
	$(LD) -m elf_i386 -T src/user/linker.ld -o test.elf $(USER_TEST_OBJ)
	$(OBJCOPY) -O binary test.elf test.bin
	
	$(LD) -m elf_i386 -T src/user/linker.ld -o shell.elf $(USER_SHELL_OBJ)
	$(OBJCOPY) -O binary shell.elf shell.bin

	$(LD) -m elf_i386 -T src/user/linker.ld -o notepad.elf $(USER_NOTEPAD_OBJ)
	$(OBJCOPY) -O binary notepad.elf notepad.bin
	
	mkdir -p Asm/boot
	python3 tools/qex_builder.py test.bin Asm/boot/test.qex
	python3 tools/qex_builder.py shell.bin Asm/boot/shell.qex
	python3 tools/qex_builder.py notepad.bin Asm/boot/notepad.qex

# ==================================================
# Image
# ==================================================
image: kernel user_bin $(FONT_ATLAS)
	mkdir -p Asm/boot
	mv kernel Asm/boot/kernel
	echo "Hello from fs!" > hello.txt
	
	dd if=/dev/zero of=disk.img bs=1M count=32
	mkfs.fat -F 16 disk.img
	
	$(MCOPY) -i disk.img Asm/boot/test.qex ::TEST.QEX
	$(MCOPY) -i disk.img Asm/boot/shell.qex ::SHELL.QEX
	$(MCOPY) -i disk.img Asm/boot/notepad.qex ::NOTEPAD.QEX
	$(MCOPY) -i disk.img hello.txt ::HELLO.TXT
	
	$(GRUB_MKRESCUE) -o kernel.iso Asm/
		
# ==================================================
# Run
# ==================================================
run: image
	qemu-system-i386 \
		-enable-kvm \
		-cpu host \
		-hda disk.img \
		-cdrom kernel.iso \
		-boot order=d

# ==================================================
# Clean
# ==================================================
clean:
	rm -f $(KERNEL_OBJ) $(USER_TEST_OBJ) $(USER_SHELL_OBJ) $(USER_NOTEPAD_OBJ) \
	      *.elf *.bin kernel disk.img kernel.iso
	rm -f Asm/boot/*.qex Asm/boot/*.bin

.PHONY: all clean kernel image run user_bin