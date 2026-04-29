# SPDX-License-Identifier: MIT
#
# Makefile - Build system for QuarkOS (aarch64)

CROSS ?= aarch64-elf-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
MCOPY   = mcopy

CFLAGS  = -ffreestanding -Wall -Wextra -O2 -g -mcpu=cortex-a57 -Isrc -MMD -MP \
           -fno-stack-protector
USER_CFLAGS = -ffreestanding -Wall -Wextra -O2 -mcpu=cortex-a57 -mstrict-align -Isrc -MMD -MP \
              -fno-stack-protector
LIBGCC  = $(shell $(CC) -mcpu=cortex-a57 -print-libgcc-file-name)
LDFLAGS = -T linker_aarch64.ld -nostdlib
USER_LDFLAGS = -T src/user/linker_aarch64.ld -nostdlib

FONT_ATLAS = Asm/boot/font_atlas.bin
ICONS_BIN = Asm/boot/icons.bin
WALLPAPER_BIN = Asm/boot/wallpaper.bin
FONT_TTF = tools/Inter-VariableFont_opsz,wght.ttf

KERNEL_C = \
	src/kernel/kernel.c \
	src/kernel/bootsplash.c \
	src/video/vga.c \
	src/util/util.c \
	src/drivers/timer.c \
	src/mm/kmalloc.c \
	src/drivers/GUI/framebuffer.c \
	src/video/font.c \
	src/gfx/events.c \
	src/lib/stdlib.c \
	src/lib/div64.c \
	src/lib/math.c \
	src/kernel/scheduler.c \
	src/kernel/elf.c \
	src/kernel/syscalls.c \
	src/fs/fat.c \
	src/gfx/font_atlas.c \
	src/gfx/bmp.c \
	src/arch/aarch64/gic.c \
	src/arch/aarch64/mmu.c \
	src/arch/aarch64/timer_arm.c \
	src/arch/aarch64/virtio_gpu.c \
	src/arch/aarch64/virtio_input.c \
	src/arch/aarch64/virtio_blk.c \
	src/arch/aarch64/uart.c \
	src/arch/aarch64/stubs.c \
	src/arch/aarch64/platform.c \
	src/kernel/klog.c

KERNEL_OBJ = $(KERNEL_C:.c=.o) \
	src/arch/aarch64/boot.o \
	src/arch/aarch64/vectors.o \
	src/arch/aarch64/context.o \
	src/arch/aarch64/modules.o

MUSIC_SPK = Asm/boot/music.spk
MUSIC_WAV = Asm/boot/music.wav
MUSIC_SRC := $(wildcard music.flac)

DEPS := $(patsubst %.c,%.d,$(KERNEL_C)) $(patsubst %.c,%.d,$(wildcard src/user/*.c src/user/lib/*.c src/lib/*.c src/kernel/init.c))
-include $(DEPS)

all: image

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

src/arch/aarch64/boot.o: src/arch/aarch64/boot.S
	$(CC) $(CFLAGS) -c $< -o $@

src/arch/aarch64/vectors.o: src/arch/aarch64/vectors.S
	$(CC) $(CFLAGS) -c $< -o $@

src/arch/aarch64/context.o: src/arch/aarch64/context.S
	$(CC) $(CFLAGS) -c $< -o $@

AARCH64_USER_DIR = build_aarch64_user
AARCH64_USER_LIB_OBJ = \
    $(AARCH64_USER_DIR)/crt0.o $(AARCH64_USER_DIR)/libapi.o $(AARCH64_USER_DIR)/lib.o \
    $(AARCH64_USER_DIR)/gfx.o $(AARCH64_USER_DIR)/window.o $(AARCH64_USER_DIR)/font_atlas.o \
    $(AARCH64_USER_DIR)/math.o $(AARCH64_USER_DIR)/stdlib.o $(AARCH64_USER_DIR)/path.o \
    $(AARCH64_USER_DIR)/storage_client.o

$(AARCH64_USER_DIR)/crt0.o: src/arch/aarch64/crt0.S
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/%.o: src/user/lib/%.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/math.o: src/lib/math.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/stdlib.o: src/lib/stdlib.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/%.o: src/user/%.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/init.o: src/kernel/init.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

AARCH64_USER_LIBGCC = $(shell aarch64-elf-gcc -mcpu=cortex-a57 -print-libgcc-file-name)

user_bin: $(ICONS_BIN) $(AARCH64_USER_LIB_OBJ) \
		$(AARCH64_USER_DIR)/storage_server.o $(AARCH64_USER_DIR)/input_server.o \
		$(AARCH64_USER_DIR)/time_server.o $(AARCH64_USER_DIR)/comp.o \
		$(AARCH64_USER_DIR)/ttysh.o $(AARCH64_USER_DIR)/init.o \
		$(AARCH64_USER_DIR)/test.o $(AARCH64_USER_DIR)/procman.o \
		$(AARCH64_USER_DIR)/paint.o $(AARCH64_USER_DIR)/terminal.o \
		$(AARCH64_USER_DIR)/filemgr.o $(AARCH64_USER_DIR)/imgview.o \
		$(AARCH64_USER_DIR)/notepad.o $(AARCH64_USER_DIR)/vkbd.o \
		$(AARCH64_USER_DIR)/vmap.o $(AARCH64_USER_DIR)/fetch.o
	mkdir -p Asm/boot
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/storage_server.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/storage_server.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/storage_server.elf $(AARCH64_USER_DIR)/storage_server.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/storage_server.bin Asm/boot/storage_server.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/input_server.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/input_server.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/input_server.elf $(AARCH64_USER_DIR)/input_server.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/input_server.bin Asm/boot/input_server.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/time_server.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/time_server.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/time_server.elf $(AARCH64_USER_DIR)/time_server.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/time_server.bin Asm/boot/time_server.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/comp.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/comp.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/comp.elf $(AARCH64_USER_DIR)/comp.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/comp.bin Asm/boot/comp.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/ttysh.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/ttysh.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/ttysh.elf $(AARCH64_USER_DIR)/ttysh.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/ttysh.bin Asm/boot/ttysh.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/init.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/init.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/init.elf $(AARCH64_USER_DIR)/init.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/init.bin Asm/boot/init.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/test.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/test.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/test.elf $(AARCH64_USER_DIR)/test.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/test.bin Asm/boot/test.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/procman.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/procman.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/procman.elf $(AARCH64_USER_DIR)/procman.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/procman.bin Asm/boot/procman.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/paint.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/paint.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/paint.elf $(AARCH64_USER_DIR)/paint.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/paint.bin Asm/boot/paint.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/terminal.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/terminal.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/terminal.elf $(AARCH64_USER_DIR)/terminal.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/terminal.bin Asm/boot/terminal.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/filemgr.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/filemgr.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/filemgr.elf $(AARCH64_USER_DIR)/filemgr.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/filemgr.bin Asm/boot/filemgr.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/imgview.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/imgview.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/imgview.elf $(AARCH64_USER_DIR)/imgview.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/imgview.bin Asm/boot/imgview.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/notepad.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/notepad.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/notepad.elf $(AARCH64_USER_DIR)/notepad.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/notepad.bin Asm/boot/notepad.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/vkbd.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/vkbd.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/vkbd.elf $(AARCH64_USER_DIR)/vkbd.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/vkbd.bin Asm/boot/vkbd.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/vmap.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/vmap.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/vmap.elf $(AARCH64_USER_DIR)/vmap.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/vmap.bin Asm/boot/vmap.qex
	$(LD) $(USER_LDFLAGS) -o $(AARCH64_USER_DIR)/fetch.elf $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/fetch.o $(AARCH64_USER_LIBGCC)
	$(OBJCOPY) -O binary $(AARCH64_USER_DIR)/fetch.elf $(AARCH64_USER_DIR)/fetch.bin
	python3 tools/qex_builder.py $(AARCH64_USER_DIR)/fetch.bin Asm/boot/fetch.qex

resources:
	$(MAKE) $(FONT_ATLAS) $(ICONS_BIN) $(WALLPAPER_BIN)

src/arch/aarch64/modules.o: src/arch/aarch64/modules.S user_bin resources
	$(CC) $(CFLAGS) -c $< -o $@

src/user/lib/crt0.o: src/arch/aarch64/crt0.S
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(FONT_ATLAS): tools/font_baker.py
	mkdir -p Asm/boot
	python3 tools/font_baker.py

$(ICONS_BIN): tools/icon_baker.py tools/lucide.ttf
	mkdir -p Asm/boot
	python3 tools/icon_baker.py

$(WALLPAPER_BIN): tools/wallpaper_baker.py wallpaper.bmp
	python3 tools/wallpaper_baker.py wallpaper.bmp Asm/boot/wallpaper.bin

Asm/boot/kaori.bmp:

Asm/boot/terry.bmp:

src/user/%.o: src/user/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/kernel/init.o: src/kernel/init.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/lib/math.o: src/lib/math.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/lib/stdlib.o: src/lib/stdlib.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

kernel: $(KERNEL_OBJ) linker_aarch64.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(LIBGCC)

$(MUSIC_SPK): music.flac
	-python3 tools/flac_to_spk.py music.flac $(MUSIC_SPK)

$(MUSIC_WAV): music.flac
	ffmpeg -y -i music.flac -ar 44100 -ac 2 -sample_fmt s16 $(MUSIC_WAV)

MUSIC_WAV_EXISTS := $(wildcard $(MUSIC_WAV))

disk: kernel user_bin $(FONT_ATLAS) $(WALLPAPER_BIN) Asm/boot/kaori.bmp Asm/boot/terry.bmp $(if $(MUSIC_SRC),$(MUSIC_SPK),) $(if $(MUSIC_SRC),$(MUSIC_WAV),)
	mkdir -p Asm/boot
	cp kernel Asm/boot/kernel
	echo "Hello from fs!" > hello.txt
	dd if=/dev/zero of=disk.img bs=1M count=64
	mkfs.fat -F 32 disk.img
	mmd -i disk.img ::PRG
	mmd -i disk.img ::SYS
	mmd -i disk.img ::SYS/BIN
	mmd -i disk.img ::SYS/DAT
	mmd -i disk.img ::SYS/LOG
	mmd -i disk.img ::USR
	mmd -i disk.img ::USR/DEFAULT
	mmd -i disk.img ::USR/DEFAULT/DESK
	mmd -i disk.img ::USR/DEFAULT/DOCS
	mmd -i disk.img ::TMP
	$(MCOPY) -i disk.img Asm/boot/test.qex ::PRG/TEST.QEX
	$(MCOPY) -i disk.img Asm/boot/notepad.qex ::PRG/NOTEPAD.QEX
	$(MCOPY) -i disk.img Asm/boot/procman.qex ::PRG/PROCMAN.QEX
	$(MCOPY) -i disk.img Asm/boot/vmap.qex ::PRG/VMAP.QEX
	$(MCOPY) -i disk.img Asm/boot/comp.qex ::PRG/COMP.QEX
	$(MCOPY) -i disk.img Asm/boot/fetch.qex ::PRG/FETCH.QEX
	$(MCOPY) -i disk.img Asm/boot/imgview.qex ::PRG/IMGVIEW.QEX
	$(MCOPY) -i disk.img Asm/boot/filemgr.qex ::PRG/FILEMGR.QEX
	$(MCOPY) -i disk.img Asm/boot/vkbd.qex ::PRG/VKBD.QEX
	$(MCOPY) -i disk.img Asm/boot/paint.qex ::PRG/PAINT.QEX
	$(MCOPY) -i disk.img Asm/boot/terminal.qex ::PRG/TERMINAL.QEX
	$(MCOPY) -i disk.img Asm/boot/ttysh.qex ::PRG/TTYSH.QEX
	$(if $(wildcard $(MUSIC_SPK)),$(MCOPY) -i disk.img $(MUSIC_SPK) ::SYS/DAT/MUSIC.SPK,)
	$(if $(MUSIC_SRC),$(MCOPY) -i disk.img $(MUSIC_WAV) ::USR/DEFAULT/DESK/MUSIC.WAV,)
	$(MCOPY) -i disk.img Asm/boot/font_atlas.bin ::SYS/DAT/ATL.BIN
	$(if $(wildcard $(FONT_TTF)),$(MCOPY) -i disk.img "$(FONT_TTF)" ::SYS/DAT/FONT.TTF,)
	$(MCOPY) -i disk.img $(ICONS_BIN) ::SYS/DAT/GUI.BIN
	$(MCOPY) -i disk.img Asm/boot/wallpaper.bin ::SYS/DAT/BG.BIN
	$(MCOPY) -i disk.img Asm/boot/logo.bmp ::SYS/DAT/LOGO.BMP
	$(MCOPY) -i disk.img Asm/boot/kaori.bmp ::USR/DEFAULT/DESK/KAORI.BMP
	$(MCOPY) -i disk.img Asm/boot/terry.bmp ::USR/DEFAULT/DESK/TERRY.BMP
	echo "QuarkOS" > hostname.txt && $(MCOPY) -i disk.img hostname.txt ::SYS/BIN/HOST
	echo "0.3.1-blanket" > version.txt && $(MCOPY) -i disk.img version.txt ::SYS/BIN/VER
	echo "/dev/ata0 / fat16 defaults 0 0" > fstab.txt && $(MCOPY) -i disk.img fstab.txt ::SYS/BIN/BOOT
	echo "Welcome to QuarkOS v0.3.1!" > readme.txt && $(MCOPY) -i disk.img readme.txt ::USR/DEFAULT/README.TXT
	echo "User Manual:\n1. Use 'guide' for help.\n2. 'drift' (cd) to change dirs.\n3. 'glimpse' (ls) to list files." > manual.txt && $(MCOPY) -i disk.img manual.txt ::USR/DEFAULT/DOCS/MANUAL.TXT
	touch empty.tmp && $(MCOPY) -i disk.img empty.tmp ::TMP/.KEEP

image: disk

run: kernel disk
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a57 \
		-m 512M \
		-rtc base=localtime \
		-kernel kernel \
		-drive if=none,file=disk.img,format=raw,readonly=on,snapshot=on,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-mouse-device \
		-nographic \
		-serial mon:stdio

run-gui: kernel disk
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a57 \
		-m 512M \
		-kernel kernel \
		-drive if=none,file=disk.img,format=raw,readonly=on,snapshot=on,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa \
		-accel tcg,thread=multi

test-disks:
	dd if=/dev/zero of=floppy.img bs=512 count=2880 2>/dev/null
	dd if=/dev/zero of=cdrom.img bs=1M count=50 2>/dev/null
	dd if=/dev/zero of=usb.img bs=1M count=8 2>/dev/null
	mkfs.fat -F 12 floppy.img >/dev/null
	mkfs.fat -F 16 cdrom.img >/dev/null
	mkfs.fat -F 12 usb.img >/dev/null
	echo "Hello from floppy!" > _floppy_hello.txt && mcopy -i floppy.img _floppy_hello.txt ::HELLO.TXT && rm -f _floppy_hello.txt
	echo "Hello from USB!" > _usb_hello.txt && mcopy -i usb.img _usb_hello.txt ::HELLO.TXT && rm -f _usb_hello.txt

run-hvf: kernel disk
	qemu-system-aarch64 \
		-M virt \
		-cpu host \
		-accel hvf \
		-m 512M \
		-rtc base=localtime \
		-kernel kernel \
		-drive if=none,file=disk.img,format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

run-hvf-multi: kernel disk test-disks
	qemu-system-aarch64 \
		-M virt \
		-cpu host \
		-accel hvf \
		-m 512M \
		-rtc base=localtime \
		-kernel kernel \
		-drive if=none,file=disk.img,format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-drive if=none,file=floppy.img,format=raw,id=vd1 \
		-device virtio-blk-device,drive=vd1 \
		-drive if=none,file=cdrom.img,format=raw,readonly=on,id=vd2 \
		-device virtio-blk-device,drive=vd2 \
		-drive if=none,file=usb.img,format=raw,id=vd3 \
		-device virtio-blk-device,drive=vd3 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

run-tcg: kernel disk
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a57 \
		-accel tcg \
		-m 512M \
		-rtc base=localtime \
		-kernel kernel \
		-drive if=none,file=disk.img,format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device,xres=1920,yres=1080 \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

clean:
	find src -name "*.d" -delete
	find src -name "*.o" -delete
	rm -f *.elf *.bin *.img kernel kernel.iso
	rm -f hello.txt fstab.txt hostname.txt readme.txt manual.txt version.txt boot.log empty.tmp
	rm -f floppy.img cdrom.img usb.img
	rm -f src/user/lib/icons_def.h
	rm -rf build_aarch64_user
	rm -f Asm/boot/*.qex Asm/boot/*.bin Asm/boot/*.elf Asm/boot/kernel

.PHONY: all clean image run run-gui run-hvf run-hvf-multi run-tcg user_bin resources test-disks
