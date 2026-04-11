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
CFLAGS = -ffreestanding -Wall -Wextra -O2 -g -m32 -Isrc -MMD -MP -fno-stack-protector
USER_CFLAGS = -ffreestanding -Wall -Wextra -O2 -m32 -Isrc -MMD -MP -fno-stack-protector
FONT_ATLAS = Asm/boot/font_atlas.bin
ICONS_BIN = Asm/boot/icons.bin
WALLPAPER_BIN = Asm/boot/wallpaper.bin

LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# ==================================================
# Files
# ==================================================
KERNEL_C = \
	src/kernel/kernel.c \
	src/kernel/bootsplash.c \
	src/kernel/crash.c \
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
	src/gfx/events.c \
	src/arch/x86/fpu.c \
	src/lib/stdlib.c \
	src/lib/div64.c \
	src/lib/math.c \
	src/kernel/scheduler.c \
	src/kernel/elf.c \
	src/kernel/syscalls.c \
	src/fs/fat.c \
	src/drivers/ata.c \
	src/drivers/pcspk.c \
	src/gfx/bmp.c \
	src/drivers/rtc.c \
	src/drivers/acpi.c \
	src/drivers/sb16.c

KERNEL_OBJ = $(KERNEL_C:.c=.o) \
	src/kernel/process.o \
	boot.o gdts.o idts.o

USER_LIB_OBJ = \
    src/user/lib/crt0.o \
    src/user/lib/libapi.o \
    src/user/lib/lib.o \
    src/user/lib/gfx.o \
    src/user/lib/window.o \
    src/user/lib/font_atlas.o \
    src/user/lib/math.o \
    src/user/lib/stdlib.o \
    src/user/lib/path.o \
    src/user/lib/storage_client.o

USER_TEST_OBJ = $(USER_LIB_OBJ) src/user/test.o
USER_SHELL_OBJ = $(USER_LIB_OBJ) src/user/shell.o
USER_COMP_OBJ = $(USER_LIB_OBJ) src/user/comp.o
USER_NOTEPAD_OBJ = $(USER_LIB_OBJ) src/user/notepad.o
USER_VMAP_OBJ = $(USER_LIB_OBJ) src/user/vmap.o
USER_SYSINFO_OBJ = $(USER_LIB_OBJ) src/user/sysinfo.o
USER_STORAGE_SERVER_OBJ = $(USER_LIB_OBJ) src/user/storage_server.o
USER_INPUT_SERVER_OBJ = $(USER_LIB_OBJ) src/user/input_server.o
USER_TIME_SERVER_OBJ = $(USER_LIB_OBJ) src/user/time_server.o
USER_INIT_OBJ = $(USER_LIB_OBJ) src/kernel/init.o
USER_ELFDEMO_OBJ  = $(USER_LIB_OBJ) src/user/elfdemo.o
USER_FETCH_OBJ    = $(USER_LIB_OBJ) src/user/fetch.o
USER_IMGVIEW_OBJ  = $(USER_LIB_OBJ) src/user/imgview.o

MUSIC_SPK = Asm/boot/music.spk
MUSIC_WAV = Asm/boot/music.wav
MUSIC_SRC := $(wildcard music.flac)

DEPS := $(patsubst %.c,%.d,$(KERNEL_C)) $(patsubst %.c,%.d,$(wildcard src/user/*.c src/user/lib/*.c src/lib/*.c src/kernel/init.c))
-include $(DEPS)

# ==================================================
# Default target
# ==================================================
all: image

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

$(ICONS_BIN): tools/icon_baker.py tools/lucide.ttf
	python3 tools/icon_baker.py

$(WALLPAPER_BIN): tools/wallpaper_baker.py wallpaper.bmp
	python3 tools/wallpaper_baker.py wallpaper.bmp Asm/boot/wallpaper.bin

Asm/boot/kaori.bmp:

Asm/boot/terry.bmp:

src/user/lib/crt0.o: src/user/lib/crt0.s
	$(AS) -f elf32 $< -o $@


src/user/%.o: src/user/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@


src/kernel/init.o: src/kernel/init.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/lib/math.o: src/lib/math.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

src/user/lib/stdlib.o: src/lib/stdlib.c
	$(CC) $(USER_CFLAGS) -c $< -o $@


kernel: $(KERNEL_OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ)

# ==================================================
# Userland
# ==================================================
$(MUSIC_SPK): music.flac
	-python3 tools/flac_to_spk.py music.flac $(MUSIC_SPK)

$(MUSIC_WAV): music.flac
	ffmpeg -y -i music.flac -ar 44100 -ac 2 -sample_fmt s16 $(MUSIC_WAV)

user_bin: $(ICONS_BIN) $(USER_TEST_OBJ) $(USER_SHELL_OBJ) $(USER_NOTEPAD_OBJ) $(USER_COMP_OBJ) \
          $(USER_STORAGE_SERVER_OBJ) $(USER_INPUT_SERVER_OBJ) $(USER_TIME_SERVER_OBJ) \
          $(USER_INIT_OBJ) $(USER_VMAP_OBJ) $(USER_SYSINFO_OBJ) $(USER_ELFDEMO_OBJ) \
          $(USER_FETCH_OBJ) $(USER_IMGVIEW_OBJ)
	mkdir -p Asm/boot
	$(LD) -m elf_i386 -T src/user/linker.ld -o test.elf $(USER_TEST_OBJ)
	cp test.elf Asm/boot/test.elf
	$(OBJCOPY) -O binary test.elf test.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o shell.elf $(USER_SHELL_OBJ)
	cp shell.elf Asm/boot/shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o notepad.elf $(USER_NOTEPAD_OBJ)
	cp notepad.elf Asm/boot/notepad.elf
	$(OBJCOPY) -O binary notepad.elf notepad.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o comp.elf $(USER_COMP_OBJ)
	cp comp.elf Asm/boot/comp.elf
	$(OBJCOPY) -O binary comp.elf comp.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o vmap.elf $(USER_VMAP_OBJ)
	cp vmap.elf Asm/boot/vmap.elf
	$(OBJCOPY) -O binary vmap.elf vmap.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o sysinfo.elf $(USER_SYSINFO_OBJ)
	cp sysinfo.elf Asm/boot/sysinfo.elf
	$(OBJCOPY) -O binary sysinfo.elf sysinfo.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o storage_server.elf $(USER_STORAGE_SERVER_OBJ)
	cp storage_server.elf Asm/boot/storage_server.elf
	$(OBJCOPY) -O binary storage_server.elf storage_server.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o input_server.elf $(USER_INPUT_SERVER_OBJ)
	cp input_server.elf Asm/boot/input_server.elf
	$(OBJCOPY) -O binary input_server.elf input_server.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o init.elf $(USER_INIT_OBJ)
	cp init.elf Asm/boot/init.elf
	$(OBJCOPY) -O binary init.elf init.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o time_server.elf $(USER_TIME_SERVER_OBJ)
	cp time_server.elf Asm/boot/time_server.elf
	$(OBJCOPY) -O binary time_server.elf time_server.bin
	$(LD) -m elf_i386 -T src/user/linker.ld -o elfdemo.elf $(USER_ELFDEMO_OBJ)
	cp elfdemo.elf Asm/boot/elfdemo.elf
	python3 tools/qex_builder.py test.bin Asm/boot/test.qex
	python3 tools/qex_builder.py shell.bin Asm/boot/shell.qex
	python3 tools/qex_builder.py notepad.bin Asm/boot/notepad.qex
	python3 tools/qex_builder.py vmap.bin Asm/boot/vmap.qex
	python3 tools/qex_builder.py comp.bin Asm/boot/comp.qex
	python3 tools/qex_builder.py storage_server.bin Asm/boot/storage_server.qex
	python3 tools/qex_builder.py input_server.bin Asm/boot/input_server.qex
	python3 tools/qex_builder.py init.bin Asm/boot/init.qex
	python3 tools/qex_builder.py time_server.bin Asm/boot/time_server.qex
	python3 tools/qex_builder.py sysinfo.bin Asm/boot/sysinfo.qex
	$(LD) -m elf_i386 -T src/user/linker.ld -o fetch.elf $(USER_FETCH_OBJ)
	cp fetch.elf Asm/boot/fetch.elf
	$(OBJCOPY) -O binary fetch.elf fetch.bin
	python3 tools/qex_builder.py fetch.bin Asm/boot/fetch.qex
	$(LD) -m elf_i386 -T src/user/linker.ld -o imgview.elf $(USER_IMGVIEW_OBJ)
	cp imgview.elf Asm/boot/imgview.elf
	$(OBJCOPY) -O binary imgview.elf imgview.bin
	python3 tools/qex_builder.py imgview.bin Asm/boot/imgview.qex


# ==================================================
# Image (используем готовое ядро)
# ==================================================
MUSIC_WAV_EXISTS := $(wildcard $(MUSIC_WAV))

image: kernel user_bin $(FONT_ATLAS) $(WALLPAPER_BIN) Asm/boot/kaori.bmp Asm/boot/terry.bmp $(if $(MUSIC_SRC),$(MUSIC_SPK),) $(if $(MUSIC_SRC),$(MUSIC_WAV),)
	mkdir -p Asm/boot
	cp kernel Asm/boot/kernel
	echo "Hello from fs!" > hello.txt
	dd if=/dev/zero of=disk.img bs=1M count=64
	mkfs.fat -F 16 disk.img
	mmd -i disk.img ::APPS
	mmd -i disk.img ::SYSTEM
	mmd -i disk.img ::SYSTEM/SETUP
	mmd -i disk.img ::SYSTEM/DATA
	mmd -i disk.img ::SYSTEM/LOGS
	mmd -i disk.img ::USERS
	mmd -i disk.img ::USERS/DEFAULT
	mmd -i disk.img ::USERS/DEFAULT/DESK
	mmd -i disk.img ::USERS/DEFAULT/DOCS
	mmd -i disk.img ::TEMP
	$(MCOPY) -i disk.img Asm/boot/test.qex ::APPS/TEST.QEX
	$(MCOPY) -i disk.img Asm/boot/shell.qex ::APPS/SHELL.QEX
	$(MCOPY) -i disk.img Asm/boot/notepad.qex ::APPS/NOTEPAD.QEX
	$(MCOPY) -i disk.img Asm/boot/sysinfo.qex ::APPS/SYSINFO.QEX
	$(MCOPY) -i disk.img Asm/boot/vmap.qex ::APPS/VMAP.QEX
	$(MCOPY) -i disk.img Asm/boot/comp.qex ::APPS/COMP.QEX
	$(MCOPY) -i disk.img Asm/boot/elfdemo.elf ::APPS/ELFDEMO.ELF
	$(MCOPY) -i disk.img Asm/boot/fetch.qex ::APPS/FETCH.QEX
	$(MCOPY) -i disk.img Asm/boot/imgview.qex ::APPS/IMGVIEW.QEX
	$(if $(wildcard $(MUSIC_SPK)),$(MCOPY) -i disk.img $(MUSIC_SPK) ::SYSTEM/DATA/MUSIC.SPK,)
	$(if $(MUSIC_SRC),$(MCOPY) -i disk.img $(MUSIC_WAV) ::USERS/DEFAULT/DESK/MUSIC.WAV,)
	$(MCOPY) -i disk.img Asm/boot/font_atlas.bin ::SYSTEM/DATA/ATL.BIN
	$(MCOPY) -i disk.img $(ICONS_BIN) ::SYSTEM/DATA/GUI.BIN
	$(MCOPY) -i disk.img Asm/boot/wallpaper.bin ::SYSTEM/DATA/BG.BIN
	$(MCOPY) -i disk.img Asm/boot/logo.bmp ::SYSTEM/DATA/LOGO.BMP
	$(MCOPY) -i disk.img Asm/boot/kaori.bmp ::USERS/DEFAULT/DESK/KAORI.BMP
	$(MCOPY) -i disk.img Asm/boot/terry.bmp ::USERS/DEFAULT/DESK/TERRY.BMP
	echo "QuarkOS" > hostname.txt && $(MCOPY) -i disk.img hostname.txt ::SYSTEM/SETUP/HOST
	echo "0.3.1-blanket" > version.txt && $(MCOPY) -i disk.img version.txt ::SYSTEM/SETUP/VER
	echo "/dev/ata0 / fat16 defaults 0 0" > fstab.txt && $(MCOPY) -i disk.img fstab.txt ::SYSTEM/SETUP/BOOT
	echo "Welcome to QuarkOS v0.3.1!" > readme.txt && $(MCOPY) -i disk.img readme.txt ::USERS/DEFAULT/README.TXT
	echo "User Manual:\n1. Use 'guide' for help.\n2. 'drift' (cd) to change dirs.\n3. 'glimpse' (ls) to list files." > manual.txt && $(MCOPY) -i disk.img manual.txt ::USERS/DEFAULT/DOCS/MANUAL.TXT
	touch empty.tmp && $(MCOPY) -i disk.img empty.tmp ::TEMP/.KEEP
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
		-boot order=d \
		-vga virtio \
		-m 4G \
		-audiodev pa,id=snd0,out.frequency=44100,out.channels=2 \
		-machine pcspk-audiodev=snd0 \
		-device sb16,audiodev=snd0 \
		-display gtk,full-screen=on	

# ==================================================
# Clean
# ==================================================
clean:
	rm -f $(KERNEL_OBJ) $(USER_TEST_OBJ) $(USER_SHELL_OBJ) $(USER_NOTEPAD_OBJ) \
	      src/user/*.o src/kernel/*.o *.elf *.bin disk.img kernel kernel.iso \
	      fstab.txt hostname.txt readme.txt manual.txt version.txt boot.log empty.tmp mdir*
	find src -name "*.d" -delete
	find src -name "*.o" -delete
	rm -f Asm/boot/*.qex Asm/boot/*.bin Asm/boot/*.elf Asm/boot/kernel

.PHONY: all clean image run user_bin
