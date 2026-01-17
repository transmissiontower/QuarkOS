# QuarkOS

![Build Status](https://img.shields.io/badge/build-passing-brightgreen) ![Arch](https://img.shields.io/badge/arch-x86-blue) ![License](https://img.shields.io/badge/license-MIT-orange)

**QuarkOS** (codenamed *Cyclone*) is a hobby operating system written from scratch in C and x86 Assembly.

The goal of this project is to understand the low-level workings of operating systems, including kernel development, memory management, driver implementation, and graphical user interface design.

> ⚠️ **Disclaimer:** This is an educational project. The code contains magic numbers, simplified algorithms (e.g., ATA PIO polling), and experimental features. It is **not** intended for production use. I am constantly learning and refactoring the architecture.

## ✨ Features

### Kernel & Architecture
*   **Architecture:** 32-bit x86 Protected Mode.
*   **Boot:** Multiboot compliant (loaded via GRUB).
*   **Memory Management:** PMM (Bitmap based) and VMM (Paging).
*   **Interrupts:** GDT, IDT, and ISR handling.
*   **Multitasking:** Preemptive round-robin scheduler with Ring 0 -> Ring 3 context switching.

### Graphics & GUI
*   **Video Mode:** VESA Linear Framebuffer (LFB).
*   **Compositor:** Custom window compositing with dirty rectangles optimization and atomic rendering.
*   **UI:** Window management system with rounded corners, shadows, alpha blending, and clipping.
*   **Fonts:** Custom font rendering engine using a baked TrueType font atlas.

### Drivers & Hardware
*   **Input:** PS/2 Keyboard and Mouse drivers (Interrupt-driven).
*   **Storage:** ATA/IDE driver (PIO Mode).
*   **Time:** Programmable Interval Timer (PIT) and Real Time Clock (RTC).

### Filesystem & User Space
*   **Filesystem:** FAT16 (Read-Only implementation).
*   **User Space:** Loading and executing custom `.qex` (Quark Executable) binaries from the virtual disk.
*   **Syscalls:** System call interface (`int 0x80`) for window creation, drawing primitives, time, and process management.

## 🛠️ Building and Running

### Prerequisites
You need a Linux environment (or WSL on Windows) with the following tools installed:

*   **Compiler:** `i686-elf-gcc`, `nasm`
*   **Build Tools:** `make`
*   **Image Creation:** `xorriso`, `grub-mkrescue` (might be `grub2-mkrescue`), `mtools`
*   **Emulator:** `qemu-system-i386`
*   **Assets Generation:** `python3` and `Pillow` library

**Install Python dependencies:**
```bash
pip install Pillow
```


**Clone the repository:**
```bash
git clone https://github.com/anticy-os/QuarkOS.git
cd QuarkOS
```
**Build and run the OS image:**
```bash
    make image
    make run
```

## 🖥️ Console (Early Demo)

QuarkOS includes a minimal console for basic commands and filesystem test.

**See list of available commands**
```bash
help
```
**Clear the console**
```bash
clear
```

**Read a text file from the fs and print it:**
```bash
cat hello.txt
```
**Load and execute a .qex (Quark Executable) binary from disk:**
```bash
exec test.qex
```
**Shutdown (currenty unavailable)**
```bash
shutdown
```

### 📜 License

This project is licensed under the MIT License. Feel free to use the code for your own learning purposes.
