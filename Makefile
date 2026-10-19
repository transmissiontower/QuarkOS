CROSS ?= aarch64-elf-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
MCOPY   = mcopy
KERNEL  = sodium.elf
KERNEL_BIN = build/sodium.bin
DISK    = sodium.img
DISK_FAT_OFFSET_SECTORS ?= 2048
DISK_FAT_OFFSET = $(shell expr $(DISK_FAT_OFFSET_SECTORS) \* 512)
UBOOT   ?= u-boot.bin
UBOOT_LOAD_ADDR ?= 0x40200000
MKIMAGE ?= $(if $(wildcard build/uboot-src/tools/mkimage),build/uboot-src/tools/mkimage,mkimage)
BOOT_SCRIPT = build/boot.scr
MKFS_FAT ?= $(shell command -v mkfs.fat 2>/dev/null || { prefix=$$(brew --prefix dosfstools 2>/dev/null); if [ -n "$$prefix" ] && [ -x "$$prefix/sbin/mkfs.fat" ]; then printf '%s' "$$prefix/sbin/mkfs.fat"; else printf '%s' mkfs.fat; fi; })

CFLAGS  = -mno-outline-atomics -ffreestanding -Wall -Wextra -O2 -g -mcpu=cortex-a57 -Isrc -MMD -MP \
           -fno-stack-protector
USER_CFLAGS = -mno-outline-atomics -ffreestanding -Wall -Wextra -O2 -mcpu=cortex-a57 -mstrict-align -Isrc -MMD -MP \
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
	src/util/util.c \
	src/drivers/timer.c \
	src/mm/kmalloc.c \
	src/drivers/GUI/framebuffer.c \
	src/video/font.c \
	src/lib/stdlib.c \
	src/lib/div64.c \
	src/lib/math.c \
	src/kernel/scheduler.c \
	src/kernel/syscalls.c \
	src/gfx/bmp.c \
	src/arch/aarch64/gic.c \
	src/arch/aarch64/mmu.c \
	src/arch/aarch64/timer_arm.c \
	src/arch/aarch64/virtio_gpu.c \
	src/arch/aarch64/virtio_input.c \
	src/arch/aarch64/virtio_blk.c \
	src/arch/aarch64/uart.c \
	src/arch/aarch64/rtc.c \
	src/arch/aarch64/platform.c \
	src/kernel/klog.c

KERNEL_OBJ = $(KERNEL_C:.c=.o) \
	src/arch/aarch64/boot.o \
	src/arch/aarch64/vectors.o \
	src/arch/aarch64/context.o \
	src/arch/aarch64/modules.o


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
    $(AARCH64_USER_DIR)/libapi.o $(AARCH64_USER_DIR)/lib.o \
    $(AARCH64_USER_DIR)/gfx.o $(AARCH64_USER_DIR)/window.o $(AARCH64_USER_DIR)/font_atlas.o \
    $(AARCH64_USER_DIR)/math.o $(AARCH64_USER_DIR)/stdlib.o $(AARCH64_USER_DIR)/path.o \
    $(AARCH64_USER_DIR)/storage_client.o

USER_APP_SRCS  := $(wildcard src/user/*.c)
USER_APP_NAMES := $(basename $(notdir $(USER_APP_SRCS)))
USER_APP_NAMES += init
USER_APP_QEXES := $(foreach n,$(USER_APP_NAMES),build/$(n).qex)

AARCH64_USER_LIBGCC = $(shell $(CC) -mcpu=cortex-a57 -print-libgcc-file-name)

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

define USER_APP_template
$(AARCH64_USER_DIR)/$(1).elf: $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/$(1).o src/user/linker_aarch64.ld
	$$(LD) $$(USER_LDFLAGS) -o $$@ $$(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/$(1).o $$(AARCH64_USER_LIBGCC)

$(AARCH64_USER_DIR)/$(1).bin: $(AARCH64_USER_DIR)/$(1).elf
	$$(OBJCOPY) -O binary $$< $$@

build/$(1).qex: $(AARCH64_USER_DIR)/$(1).bin
	@mkdir -p build
	python3 tools/qex_builder.py $$< $$@
endef

$(foreach app,$(USER_APP_NAMES),$(eval $(call USER_APP_template,$(app))))

user_bin: $(ICONS_BIN) $(USER_APP_QEXES)

resources:
	$(MAKE) $(FONT_ATLAS) $(ICONS_BIN) $(WALLPAPER_BIN)

build/logo.bmp: assets/sys/logo.bmp
	@mkdir -p build
	cp $< $@

src/arch/aarch64/modules.o: src/arch/aarch64/modules.S user_bin resources build/logo.bmp
	$(CC) $(CFLAGS) -c $< -o $@

$(FONT_ATLAS): tools/font_baker.py
	mkdir -p build
	python3 tools/font_baker.py

$(ICONS_BIN): tools/icon_baker.py tools/lucide.ttf
	mkdir -p build
	python3 tools/icon_baker.py

$(WALLPAPER_BIN): tools/wallpaper_baker.py assets/sys/wallpaper.bmp
	python3 tools/wallpaper_baker.py assets/sys/wallpaper.bmp build/wallpaper.bin


$(KERNEL): $(KERNEL_OBJ) linker_aarch64.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(LIBGCC)

$(KERNEL_BIN): $(KERNEL)
	@mkdir -p build
	$(OBJCOPY) -O binary $< $@

$(BOOT_SCRIPT): tools/uboot/boot.cmd
	@mkdir -p build
	$(MKIMAGE) -A arm64 -T script -C none -n "Sodium U-Boot boot" -d $< $@

disk: $(KERNEL_BIN) $(BOOT_SCRIPT) user_bin $(FONT_ATLAS) $(WALLPAPER_BIN)
	mkdir -p build
	dd if=/dev/zero of=$(DISK) bs=1M count=64
	python3 tools/mbr_partition.py $(DISK) $(DISK_FAT_OFFSET_SECTORS)
	$(MKFS_FAT) -F 32 --offset=$(DISK_FAT_OFFSET_SECTORS) $(DISK)
	$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) $(KERNEL_BIN) ::SODIUM.BIN
	$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) $(BOOT_SCRIPT) ::BOOT.SCR
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::PRG
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::SYS
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::SYS/BIN
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::SYS/DAT
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::SYS/LOG
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::USR
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::USR/DEFAULT
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::USR/DEFAULT/DESK
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::USR/DEFAULT/DOCS
	mmd -i $(DISK)@@$(DISK_FAT_OFFSET) ::TMP
	$(foreach n,$(USER_APP_NAMES),$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) build/$(n).qex ::PRG/$(shell echo $(n) | tr '[:lower:]' '[:upper:]').QEX ;)
	$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) build/font_atlas.bin ::SYS/DAT/ATL.BIN
	$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) $(ICONS_BIN) ::SYS/DAT/GUI.BIN
	$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) build/wallpaper.bin ::SYS/DAT/BG.BIN
	$(foreach f,$(ASSETS_SYS),$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) $(f) ::SYS/DAT/$(shell echo $(notdir $(f)) | tr '[:lower:]' '[:upper:]') ;)
	$(foreach f,$(ASSETS_DESK),$(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) $(f) ::USR/DEFAULT/DESK/$(shell echo $(notdir $(f)) | tr '[:lower:]' '[:upper:]') ;)
	touch empty.tmp && $(MCOPY) -i $(DISK)@@$(DISK_FAT_OFFSET) empty.tmp ::TMP/.KEEP

image: disk

run: $(UBOOT) disk
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu cortex-a57 \
		-m 512M \
		-rtc base=localtime \
		-device loader,file=$(UBOOT),addr=$(UBOOT_LOAD_ADDR),cpu-num=0 \
		-drive if=none,file=$(DISK),format=raw,readonly=on,snapshot=on,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-mouse-device \
		-nographic \
		-serial mon:stdio

run-gui: $(UBOOT) disk
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu cortex-a57 \
		-m 512M \
		-device loader,file=$(UBOOT),addr=$(UBOOT_LOAD_ADDR),cpu-num=0 \
		-drive if=none,file=$(DISK),format=raw,readonly=on,snapshot=on,id=vd0 \
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
	$(MKFS_FAT) -F 12 floppy.img >/dev/null
	$(MKFS_FAT) -F 16 cdrom.img >/dev/null
	$(MKFS_FAT) -F 12 usb.img >/dev/null
	echo "Hello from floppy!" > _floppy_hello.txt && mcopy -i floppy.img _floppy_hello.txt ::HELLO.TXT && rm -f _floppy_hello.txt
	echo "Hello from USB!" > _usb_hello.txt && mcopy -i usb.img _usb_hello.txt ::HELLO.TXT && rm -f _usb_hello.txt

run-hvf: $(UBOOT) disk
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu host \
		-accel hvf \
		-m 512M \
		-rtc base=localtime \
		-device loader,file=$(UBOOT),addr=$(UBOOT_LOAD_ADDR),cpu-num=0 \
		-drive if=none,file=$(DISK),format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

run-hvf-multi: $(UBOOT) disk test-disks
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu host \
		-accel hvf \
		-m 128M \
		-rtc base=localtime \
		-device loader,file=$(UBOOT),addr=$(UBOOT_LOAD_ADDR),cpu-num=0 \
		-drive if=none,file=$(DISK),format=raw,id=vd0 \
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

run-tcg: $(UBOOT) disk
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu cortex-a57 \
		-accel tcg \
		-m 512M \
		-rtc base=localtime \
		-device loader,file=$(UBOOT),addr=$(UBOOT_LOAD_ADDR),cpu-num=0 \
		-drive if=none,file=$(DISK),format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device,xres=1920,yres=1080 \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

clean:
	find src -name "*.d" -delete
	find src -name "*.o" -delete
	rm -f $(KERNEL) $(DISK)
	rm -f boot.log empty.tmp
	rm -f floppy.img cdrom.img usb.img
	rm -f src/user/lib/icons_def.h
	rm -rf build_aarch64_user
	rm -f build/*.qex build/*.bin build/*.elf build/logo.bmp

.PHONY: all clean image disk run run-gui run-hvf run-hvf-multi run-tcg user_bin resources test-disks
