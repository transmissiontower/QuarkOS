gcc = /usr/opt/cross/bin/i686-elf-gcc
ld = /usr/opt/cross/bin/i686-elf-ld
objcopy = /usr/opt/cross/bin/i686-elf-objcopy
MCOPY = mcopy

CFLAGS = -ffreestanding -Wall -Wextra -Isrc -O2 -g
USER_CFLAGS = -ffreestanding -Wall -Wextra -Isrc -m32

all: kernel user_bin boot image

clean:
	rm -rf *.o src/kernel/*.o src/user/*.o src/user/lib/*.o Asm/boot/*.qex *.bin *.elf kernel hello.txt

kernel:
	$(gcc) $(CFLAGS) -c src/kernel/kernel.c -o kernel.o
	$(gcc) $(CFLAGS) -c src/video/vga.c -o vga.o
	$(gcc) $(CFLAGS) -c src/arch/x86/gdt.c -o gdt.o
	$(gcc) $(CFLAGS) -c src/util/util.c -o util.o
	$(gcc) $(CFLAGS) -c src/arch/x86/idt.c -o idt.o
	$(gcc) $(CFLAGS) -c src/drivers/timer.c -o timer.o
	$(gcc) $(CFLAGS) -c src/drivers/ps2/keyboard.c -o keyboard.o
	$(gcc) $(CFLAGS) -c src/mm/memory.c -o memory.o
	$(gcc) $(CFLAGS) -c src/mm/kmalloc.c -o kmalloc.o
	$(gcc) $(CFLAGS) -c src/drivers/GUI/framebuffer.c -o framebuffer.o
	$(gcc) $(CFLAGS) -c src/video/font.c -o font.o
	$(gcc) $(CFLAGS) -c src/drivers/ps2/mouse.c -o mouse.o
	$(gcc) $(CFLAGS) -c src/video/window.c -o window.o
	$(gcc) $(CFLAGS) -c src/gfx/events.c -o events.o
	$(gcc) $(CFLAGS) -c src/arch/x86/fpu.c -o fpu.o
	$(gcc) $(CFLAGS) -c src/lib/math.c -o math.o
	$(gcc) $(CFLAGS) -c src/lib/stdlib.c -o stdlib.o
	$(gcc) $(CFLAGS) -c src/video/texture.c -o texture.o
	$(gcc) $(CFLAGS) -c src/video/compositor.c -o compositor.o
	$(gcc) $(CFLAGS) -c src/gfx/bmp.c -o bmp.o
	$(gcc) $(CFLAGS) -c src/gfx/font_atlas.c -o font_atlas.o
	$(gcc) $(CFLAGS) -c src/drivers/rtc.c -o rtc.o
	$(gcc) $(CFLAGS) -c src/kernel/shell.c -o shell.o
	$(gcc) $(CFLAGS) -c src/kernel/scheduler.c -o scheduler.o
	$(gcc) $(CFLAGS) -c src/kernel/syscalls.c -o syscalls.o
	$(gcc) $(CFLAGS) -c src/fs/fat.c -o fat.o
	$(gcc) $(CFLAGS) -c src/drivers/ata.c -o ata.o
	nasm -f elf32 src/kernel/process.s -o process.o

user_bin:
	mkdir -p Asm/boot
	nasm -f elf32 src/user/lib/crt0.s -o src/user/lib/crt0.o
	$(gcc) $(USER_CFLAGS) -c src/user/lib/libapi.c -o src/user/lib/libapi.o
	
	$(gcc) $(USER_CFLAGS) -c src/user/test.c -o src/user/test.o
	
	$(ld) -m elf_i386 -T src/user/linker.ld -o test.elf src/user/lib/crt0.o src/user/test.o src/user/lib/libapi.o
	
	$(objcopy) -O binary test.elf test.bin
	python3 tools/qex_builder.py test.bin Asm/boot/test.qex

boot:
	nasm -f elf32 src/boot/boot.s -o boot.o
	nasm -f elf32 src/arch/x86/gdt.s -o gdts.o
	nasm -f elf32 src/arch/x86/idt.s -o idts.o

image: kernel boot user_bin
	$(ld) -m elf_i386 -T linker.ld -o kernel boot.o kernel.o vga.o gdt.o gdts.o util.o idt.o idts.o timer.o keyboard.o memory.o kmalloc.o framebuffer.o font.o mouse.o window.o events.o fpu.o math.o stdlib.o texture.o compositor.o bmp.o font_atlas.o rtc.o shell.o process.o scheduler.o syscalls.o fat.o ata.o
	
	mv kernel Asm/boot/kernel
	
	dd if=/dev/zero of=disk.img bs=1M count=32
	mkfs.fat -F 16 disk.img
	
	$(MCOPY) -i disk.img Asm/boot/test.qex ::TEST.QEX
	
	echo "Success!" > hello.txt
	$(MCOPY) -i disk.img hello.txt ::HELLO.TXT
	rm hello.txt
	
	grub2-mkrescue -o kernel.iso Asm/
	make clean