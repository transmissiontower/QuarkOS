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

FONT_ATLAS = build/font_atlas.bin
ICONS_BIN = build/icons.bin
WALLPAPER_BIN = build/wallpaper.bin

ASSETS_DESK := $(wildcard assets/desk/*)
ASSETS_SYS  := $(wildcard assets/sys/*)

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

MUSIC_SPK = build/music.spk
MUSIC_WAV = build/music.wav
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

# Auto-discover user apps: every src/user/*.c becomes a .qex
# WIP/broken files go in src/user/wip/ — ignored automatically
USER_APP_SRCS  := $(wildcard src/user/*.c)
USER_APP_NAMES := $(basename $(notdir $(USER_APP_SRCS)))
# init comes from src/kernel/init.c — add it separately
USER_APP_NAMES += init
USER_APP_QEXES := $(foreach n,$(USER_APP_NAMES),build/$(n).qex)

AARCH64_USER_LIBGCC = $(shell aarch64-elf-gcc -mcpu=cortex-a57 -print-libgcc-file-name)

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

# Template: elf → bin → qex for each user app
define USER_APP_template
$(AARCH64_USER_DIR)/$(1).elf: $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/$(1).o
	$$(LD) $$(USER_LDFLAGS) -o $$@ $$(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/$(1).o $$(AARCH64_USER_LIBGCC)

$(AARCH64_USER_DIR)/$(1).bin: $(AARCH64_USER_DIR)/$(1).elf
	$$(OBJCOPY) -O binary $$< $$@

build/$(1).qex: $(AARCH64_USER_DIR)/$(1).bin
	@mkdir -p build
	python3 tools/qex_builder.py $$< $$@
endef

$(foreach app,$(USER_APP_NAMES),$(eval $(call USER_APP_template,$(app))))

doom_bin: $(AARCH64_USER_LIB_OBJ)
	$(MAKE) -f src/user/doom/Makefile.doom

user_bin: $(ICONS_BIN) $(USER_APP_QEXES) doom_bin

resources:
	$(MAKE) $(FONT_ATLAS) $(ICONS_BIN) $(WALLPAPER_BIN)

build/logo.bmp: assets/sys/logo.bmp
	@mkdir -p build
	cp $< $@

src/arch/aarch64/modules.o: src/arch/aarch64/modules.S user_bin resources build/logo.bmp
	$(CC) $(CFLAGS) -c $< -o $@

src/user/lib/crt0.o: src/arch/aarch64/crt0.S
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(FONT_ATLAS): tools/font_baker.py
	mkdir -p build
	python3 tools/font_baker.py

$(ICONS_BIN): tools/icon_baker.py tools/lucide.ttf
	mkdir -p build
	python3 tools/icon_baker.py

$(WALLPAPER_BIN): tools/wallpaper_baker.py assets/sys/wallpaper.bmp
	python3 tools/wallpaper_baker.py assets/sys/wallpaper.bmp build/wallpaper.bin


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

disk: kernel user_bin $(FONT_ATLAS) $(WALLPAPER_BIN) $(if $(MUSIC_SRC),$(MUSIC_SPK),)
	mkdir -p build
	cp kernel build/kernel
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
	$(foreach n,$(USER_APP_NAMES),$(MCOPY) -i disk.img build/$(n).qex ::PRG/$(shell echo $(n) | tr '[:lower:]' '[:upper:]').QEX ;)
	$(if $(wildcard build/doom.qex),$(MCOPY) -i disk.img build/doom.qex ::PRG/DOOM.QEX,)
	$(if $(wildcard src/user/doom/doom1.wad),$(MCOPY) -i disk.img src/user/doom/doom1.wad ::PRG/DOOM1.WAD,)
	$(if $(wildcard $(MUSIC_SPK)),$(MCOPY) -i disk.img $(MUSIC_SPK) ::SYS/DAT/MUSIC.SPK,)
	$(MCOPY) -i disk.img build/font_atlas.bin ::SYS/DAT/ATL.BIN
	$(MCOPY) -i disk.img $(ICONS_BIN) ::SYS/DAT/GUI.BIN
	$(MCOPY) -i disk.img build/wallpaper.bin ::SYS/DAT/BG.BIN
	$(foreach f,$(ASSETS_SYS),$(MCOPY) -i disk.img $(f) ::SYS/DAT/$(shell echo $(notdir $(f)) | tr '[:lower:]' '[:upper:]') ;)
	$(foreach f,$(ASSETS_DESK),$(MCOPY) -i disk.img $(f) ::USR/DEFAULT/DESK/$(shell echo $(notdir $(f)) | tr '[:lower:]' '[:upper:]') ;)
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
		-m 128M \
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
	rm -f boot.log empty.tmp
	rm -f floppy.img cdrom.img usb.img
	rm -f src/user/lib/icons_def.h
	rm -rf build_aarch64_user
	rm -f build/*.qex build/*.bin build/*.elf build/kernel build/logo.bmp

.PHONY: all clean image run run-gui run-hvf run-hvf-multi run-tcg user_bin resources test-disks
