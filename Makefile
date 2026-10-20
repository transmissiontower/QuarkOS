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
UBOOT   ?= boot/uboot/u-boot.bin
UBOOT_SRC ?= build/uboot-src
UBOOT_REPO ?= https://github.com/u-boot/u-boot.git
UBOOT_VERSION ?= v2026.04
JOBS ?= 4
UBOOT_MAKE ?= $(shell command -v gmake 2>/dev/null || command -v make)
UBOOT_BASH_DIR ?= $(shell brew --prefix bash 2>/dev/null)/bin
UBOOT_BUILD_PATH = $(if $(wildcard $(UBOOT_BASH_DIR)/bash),$(UBOOT_BASH_DIR):$(PATH),$(PATH))
UBOOT_OPENSSL3_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
UBOOT_PKG_CONFIG_PATH = $(if $(UBOOT_OPENSSL3_PREFIX),$(UBOOT_OPENSSL3_PREFIX)/lib/pkgconfig,$(PKG_CONFIG_PATH))
UBOOT_HOST_CFLAGS ?= $(shell PKG_CONFIG_PATH="$(UBOOT_PKG_CONFIG_PATH)" pkg-config --cflags openssl 2>/dev/null)
UBOOT_HOST_LDFLAGS ?= $(shell PKG_CONFIG_PATH="$(UBOOT_PKG_CONFIG_PATH)" pkg-config --libs openssl 2>/dev/null)
MKIMAGE ?= $(if $(wildcard $(UBOOT_SRC)/tools/mkimage),$(UBOOT_SRC)/tools/mkimage,mkimage)
BOOT_SCRIPT = build/boot.scr
MKFS_FAT ?= $(shell command -v mkfs.fat 2>/dev/null || { prefix=$$(brew --prefix dosfstools 2>/dev/null); if [ -n "$$prefix" ] && [ -x "$$prefix/sbin/mkfs.fat" ]; then printf '%s' "$$prefix/sbin/mkfs.fat"; else printf '%s' mkfs.fat; fi; })

CFLAGS  = -mno-outline-atomics -ffreestanding -Wall -Wextra -O2 -g -mcpu=cortex-a57 -Isrc -Isrc/include -MMD -MP \
           -fno-stack-protector
USER_CFLAGS = -mno-outline-atomics -ffreestanding -Wall -Wextra -O2 -mcpu=cortex-a57 -mstrict-align -Isrc -Isrc/include -MMD -MP \
              -fno-stack-protector
LIBGCC  = $(shell $(CC) -mcpu=cortex-a57 -print-libgcc-file-name)
LDFLAGS = -T src/arch/aarch64/kernel.ld -nostdlib
USER_LDFLAGS = -T src/user/linker.ld -nostdlib

FONT_ATLAS = build/font_atlas.bin
ICONS_BIN = build/icons.bin
WALLPAPER_BIN = build/wallpaper.bin

ASSETS_DESK := $(wildcard assets/desk/*)
ASSETS_SYS  := $(wildcard assets/images/*)

KERNEL_C = src/kernel/core/kernel.c src/kernel/core/tick.c src/drivers/video/bootsplash.c src/kernel/lib/util.c \
 src/drivers/timer.c src/kernel/mm/kmalloc.c src/drivers/video/framebuffer.c \
 src/drivers/video/console_font.c src/kernel/lib/kstdlib.c src/kernel/lib/div64.c \
 src/kernel/lib/math.c src/kernel/core/scheduler.c src/kernel/core/syscalls.c \
 src/drivers/video/bmp.c src/arch/aarch64/gic.c src/arch/aarch64/mmu.c \
 src/arch/aarch64/arch_timer.c src/drivers/virtio/virtio_gpu.c src/drivers/virtio/virtio_input.c \
 src/drivers/virtio/virtio_blk.c src/drivers/uart/uart_pl011.c src/drivers/rtc/rtc_pl031.c \
 src/arch/aarch64/platform.c src/kernel/core/klog.c
KERNEL_OBJ = $(KERNEL_C:.c=.o) src/arch/aarch64/boot.o src/arch/aarch64/vectors.o src/arch/aarch64/context.o src/arch/aarch64/modules.o
DEPS := $(patsubst %.c,%.d,$(KERNEL_C)) $(patsubst %.c,%.d,$(shell find src/user -name '*.c'))
-include $(DEPS)

all: image

$(UBOOT):
	@mkdir -p $(dir $@) $(dir $(UBOOT_SRC))
	@if [ ! -d "$(UBOOT_SRC)/.git" ]; then git clone --depth 1 --branch $(UBOOT_VERSION) $(UBOOT_REPO) $(UBOOT_SRC); fi
	$(UBOOT_MAKE) -C $(UBOOT_SRC) distclean
	PATH="$(UBOOT_BUILD_PATH)" $(UBOOT_MAKE) -C $(UBOOT_SRC) qemu_arm64_defconfig CROSS_COMPILE=$(CROSS) HOSTCFLAGS="$(UBOOT_HOST_CFLAGS)" HOSTLDFLAGS="$(UBOOT_HOST_LDFLAGS)"
	PATH="$(UBOOT_BUILD_PATH)" $(UBOOT_MAKE) -C $(UBOOT_SRC) CROSS_COMPILE=$(CROSS) HOSTCFLAGS="$(UBOOT_HOST_CFLAGS)" HOSTLDFLAGS="$(UBOOT_HOST_LDFLAGS)" -j$(JOBS)
	cp $(UBOOT_SRC)/u-boot.bin $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
src/arch/aarch64/boot.o: src/arch/aarch64/boot.S
	$(CC) $(CFLAGS) -c $< -o $@
src/arch/aarch64/vectors.o: src/arch/aarch64/vectors.S
	$(CC) $(CFLAGS) -c $< -o $@
src/arch/aarch64/context.o: src/arch/aarch64/context.S
	$(CC) $(CFLAGS) -c $< -o $@

AARCH64_USER_DIR = build_aarch64_user
AARCH64_USER_START_OBJ = $(AARCH64_USER_DIR)/start.o
AARCH64_USER_LIB_OBJ = $(addprefix $(AARCH64_USER_DIR)/,libapi.o lib.o gfx.o window.o font_atlas.o math.o stdlib.o path.o storage_client.o)
USER_APP_SRCS := $(wildcard src/user/apps/*.c) $(wildcard src/user/servers/*/*.c) src/user/init/init.c
USER_APP_NAMES := $(basename $(notdir $(USER_APP_SRCS)))
USER_APP_QEXES := $(foreach n,$(USER_APP_NAMES),build/$(n).qex) build/comp.qex
AARCH64_USER_LIBGCC = $(shell $(CC) -mcpu=cortex-a57 -print-libgcc-file-name)

$(AARCH64_USER_DIR)/%.o: src/user/apps/%.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_START_OBJ): src/user/libc/start.S
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_DIR)/init.o: src/user/init/init.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_DIR)/compositor.o: src/user/servers/compositor/compositor.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_DIR)/input_server.o: src/user/servers/input/input_server.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_DIR)/time_server.o: src/user/servers/time/time_server.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(AARCH64_USER_DIR)/storage_server.o: src/user/servers/storage/storage_server.c
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(AARCH64_USER_DIR)/libapi.o: src/user/libc/libapi.c
$(AARCH64_USER_DIR)/lib.o: src/user/libc/lib.c
$(AARCH64_USER_DIR)/path.o: src/user/libc/path.c
$(AARCH64_USER_DIR)/storage_client.o: src/user/clients/storage_client.c
$(AARCH64_USER_DIR)/gfx.o: src/user/libui/gfx.c
$(AARCH64_USER_DIR)/window.o: src/user/libui/window.c
$(AARCH64_USER_DIR)/font_atlas.o: src/user/libui/font_atlas.c
$(AARCH64_USER_DIR)/math.o: src/kernel/lib/math.c
$(AARCH64_USER_DIR)/stdlib.o: src/kernel/lib/kstdlib.c
$(AARCH64_USER_LIB_OBJ):
	@mkdir -p $(AARCH64_USER_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

define USER_APP_template
$(AARCH64_USER_DIR)/$(1).elf: $(AARCH64_USER_START_OBJ) $(AARCH64_USER_LIB_OBJ) $(AARCH64_USER_DIR)/$(1).o src/user/linker.ld
	$$(LD) $$(USER_LDFLAGS) -o $$@ $$(AARCH64_USER_START_OBJ) $$(AARCH64_USER_LIB_OBJ) $$(AARCH64_USER_DIR)/$(1).o $$(AARCH64_USER_LIBGCC)
$(AARCH64_USER_DIR)/$(1).bin: $(AARCH64_USER_DIR)/$(1).elf
	$$(OBJCOPY) -O binary $$< $$@
build/$(1).qex: $(AARCH64_USER_DIR)/$(1).bin
	@mkdir -p build
	python3 tools/qex_builder.py $$< $$@
endef
$(foreach app,$(USER_APP_NAMES),$(eval $(call USER_APP_template,$(app))))

build/comp.qex: build/compositor.qex
	cp $< $@

user_bin: $(ICONS_BIN) $(USER_APP_QEXES)

resources:
	$(MAKE) $(FONT_ATLAS) $(ICONS_BIN) $(WALLPAPER_BIN)

build/logo.bmp: assets/images/logo.bmp
	@mkdir -p build
	cp $< $@

src/arch/aarch64/modules.o: src/arch/aarch64/modules.S user_bin resources build/logo.bmp
	$(CC) $(CFLAGS) -c $< -o $@

$(FONT_ATLAS): tools/font_baker.py assets/fonts/inter.ttf
	mkdir -p build
	python3 tools/font_baker.py

$(ICONS_BIN): tools/icon_baker.py assets/fonts/lucide.ttf
	mkdir -p build
	python3 tools/icon_baker.py

$(WALLPAPER_BIN): tools/wallpaper_baker.py assets/images/wallpaper.bmp
	python3 tools/wallpaper_baker.py assets/images/wallpaper.bmp build/wallpaper.bin


$(KERNEL): $(KERNEL_OBJ) src/arch/aarch64/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(LIBGCC)

$(KERNEL_BIN): $(KERNEL)
	@mkdir -p build
	$(OBJCOPY) -O binary $< $@

$(BOOT_SCRIPT): boot/uboot/boot.cmd
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
		-bios $(UBOOT) \
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
		-bios $(UBOOT) \
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

# QEMU 11.1's HVF backend asserts on a data abort during the U-Boot path.
# Boot the ELF directly for HVF runs; the kernel embeds its user modules.
run-hvf: disk
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu host \
		-accel hvf \
		-m 512M \
		-rtc base=localtime \
		-kernel $(KERNEL) \
		-drive if=none,file=$(DISK),format=raw,id=vd0 \
		-device virtio-blk-device,drive=vd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-serial stdio \
		-display cocoa,zoom-to-fit=on

run-hvf-multi: disk test-disks
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-cpu host \
		-accel hvf \
		-m 128M \
		-rtc base=localtime \
		-kernel $(KERNEL) \
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
		-bios $(UBOOT) \
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
	rm -f src/user/libui/icons_def.h
	rm -rf build_aarch64_user
	rm -f build/*.qex build/*.bin build/*.elf build/logo.bmp

.PHONY: all clean image disk run run-gui run-hvf run-hvf-multi run-tcg user_bin resources test-disks
