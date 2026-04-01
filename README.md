# QuarkOS Anticyclone 1.0 (Blanket Core 0.3.1)

![Build Status](https://img.shields.io/badge/build-passing-brightgreen) ![Arch](https://img.shields.io/badge/arch-x86-blue) ![License](https://img.shields.io/badge/license-MIT-orange)

**QuarkOS Anticyclone 1.0** layers a refreshed user experience on top of the *Blanket* 0.3.1 core. It is a hobby operating system for system programmers who want to instrument kernels, drivers, and user-space servers inside one lab-friendly codebase.

The goal of this project is to understand the low-level workings of operating systems, including kernel development, memory management, driver implementation, and graphical user interface design.

> 🚨 **Contest Drop:** Anticyclone 1.0 is the contest showcase build. It inherits the instability of Blanket 0.3.1 and exposes even more experimental tooling—expect crashes between commits.

> ⚠️ **Disclaimer:** This is an educational project. The code contains magic numbers, simplified algorithms (e.g., ATA PIO polling), and experimental features. It is **not** intended for production use. I am constantly learning and refactoring the architecture.

> 🧑‍💻 **Audience:** Crafted for system programmers who want to poke at schedulers, IPC, filesystem drivers, and compositor plumbing without leaving a single repo.

## 🚧 Release Status

- **Userland tag:** `Anticyclone 1.0`
- **Kernel/core:** `0.3.1-blanket` (see `version.txt`)
- **Stability:** Contest-grade, expect driver swaps, panics, and unfinished flows
- **Focus:** Demonstrate the composited GUI stack, user-space servers, and diagnostics tooling to reviewers

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

## 🆕 Anticyclone 1.0 Additions

*   **Task Manager (`procman`):** GUI system monitor (see `src/user/sysinfo.c`) that surfaces CPU load, memory usage, process states, and lets you kill tasks directly from the window.
*   **Kernel Memory Map Viewer (`vmap`):** Visualizes kernel/user regions, framebuffer placement, and stack slots (`src/user/vmap.c`) for quick MMU sanity checks.
*   **Plasma Demo (`test`):** Real-time plasma shader stressing IPC throughput (`src/user/test.c`) doubles as a compositor burn-in.
*   **User-space servers:** Dedicated `storage_server`, `input_server`, and `time_server` binaries keep drivers small and make IPC boundaries obvious for experimentation (`src/user/*.c`).
*   **Diagnostics widgets:** `sysgraph` and `memscope` shell commands provide textual visualization of scheduler jitter and virtual-memory snapshots without leaving the console.

## 🧰 New Shell Commands & Aliases

Anticyclone refreshes the Quark shell (`src/user/shell.c`) with descriptive verbs and playful aliases so you can script kernel probes quickly:

- `guide`/`help` – inline reference for filesystem, app, and debug verbs.
- `ls`/`glimpse`, `drift`/`cd`, `beam`/`cat`, `touch`/`seed`, `mkdir`/`hatch` – filesystem navigation plus quick file seeding.
- `quill`/`nano` & `.save/.quit` – built-in editor with scrollback and Ctrl+C escape, tuned for editing `.TXT` assets inside the emulator.
- `quartz`/`hex` – hexdump helper for inspecting raw sectors.
- `procman`, `sysinfo`, `notepad`, `exec <path>` – spawns GUI apps or executes arbitrary `.qex` binaries.
- `sysgraph`, `memscope`, `uptime`, `procs`, `kill <pid>` – lightweight diagnostics for scheduler load, virtual memory, system uptime, and process control.
- `pf`/`pagefault` and `kp`/`panic` – chaos-mode traps that deliberately fault the kernel so you can test crash recovery.

## 🔌 New Drivers & Services

- **Driver manager bootstrap:** Blanket core now auto-loads the VGA, PS/2, ATA, RTC, and ACPI drivers so GUI apps, timers, and storage are ready as soon as the desktop appears.
- **ChronoFS block layer:** Journaling-friendly disk access backs the storage server flow, making file copies and app installs resilient between reboots.
- **USB mass storage shim:** Hot-swap friendly layer that lets you mount additional disks for assets and crash dumps directly from the shell.

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
exec shell.qex
exec notepad.qex
```
**Shutdown (currenty unavailable)**
```bash
shutdown
```

### 📜 License

This project is licensed under the MIT License. Feel free to use the code for your own learning purposes.
