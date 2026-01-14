#include "drivers/ps2/keyboard.h"
#include "arch/x86/idt.h"
#include "gfx/events.h"
#include "lib/stdint.h"
#include "util/util.h"
#include "video/vga.h"

static bool shift_pressed = false;
static bool caps_lock = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;

const uint32_t UNKNOWN = 0xFFFFFFFF;
const uint32_t ESC = 0xFFFFFFFF - 1;
const uint32_t CTRL = 0xFFFFFFFF - 2;
const uint32_t LSHFT = 0xFFFFFFFF - 3;
const uint32_t RSHFT = 0xFFFFFFFF - 4;
const uint32_t ALT = 0xFFFFFFFF - 5;
const uint32_t F1 = 0xFFFFFFFF - 6;
const uint32_t F2 = 0xFFFFFFFF - 7;
const uint32_t F3 = 0xFFFFFFFF - 8;
const uint32_t F4 = 0xFFFFFFFF - 9;
const uint32_t F5 = 0xFFFFFFFF - 10;
const uint32_t F6 = 0xFFFFFFFF - 11;
const uint32_t F7 = 0xFFFFFFFF - 12;
const uint32_t F8 = 0xFFFFFFFF - 13;
const uint32_t F9 = 0xFFFFFFFF - 14;
const uint32_t F10 = 0xFFFFFFFF - 15;
const uint32_t F11 = 0xFFFFFFFF - 16;
const uint32_t F12 = 0xFFFFFFFF - 17;
const uint32_t SCRLCK = 0xFFFFFFFF - 18;
const uint32_t HOME = 0xFFFFFFFF - 19;
const uint32_t UP = 0xFFFFFFFF - 20;
const uint32_t LEFT = 0xFFFFFFFF - 21;
const uint32_t RIGHT = 0xFFFFFFFF - 22;
const uint32_t DOWN = 0xFFFFFFFF - 23;
const uint32_t PGUP = 0xFFFFFFFF - 24;
const uint32_t PGDOWN = 0xFFFFFFFF - 25;
const uint32_t END = 0xFFFFFFFF - 26;
const uint32_t INS = 0xFFFFFFFF - 27;
const uint32_t DEL = 0xFFFFFFFF - 28;
const uint32_t CAPS = 0xFFFFFFFF - 29;
const uint32_t NONE = 0xFFFFFFFF - 30;
const uint32_t ALTGR = 0xFFFFFFFF - 31;
const uint32_t NUMLCK = 0xFFFFFFFF - 32;

const uint32_t lowercase[128] = { UNKNOWN, ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', CTRL, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`', LSHFT, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', RSHFT, '*', ALT, ' ', CAPS, F1, F2,
    F3, F4, F5, F6, F7, F8, F9, F10, NUMLCK, SCRLCK, HOME, UP, PGUP, '-', LEFT, UNKNOWN, RIGHT, '+', END, DOWN, PGDOWN,
    INS, DEL, UNKNOWN, UNKNOWN, UNKNOWN, F11, F12, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN };

const uint32_t uppercase[128] = { UNKNOWN, ESC, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', CTRL, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"', '~', LSHFT, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', RSHFT, '*', ALT, ' ', CAPS, F1, F2, F3,
    F4, F5, F6, F7, F8, F9, F10, NUMLCK, SCRLCK, HOME, UP, PGUP, '-', LEFT, UNKNOWN, RIGHT, '+', END, DOWN, PGDOWN, INS,
    DEL, UNKNOWN, UNKNOWN, UNKNOWN, F11, F12, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN };

static void process_scancode(uint8_t scan_code) {
    if (scan_code == 0xE0 || scan_code == 0xE1)
        return;

    bool is_released = scan_code & 0x80;

    uint8_t code = scan_code & 0x7F;

    if (code >= 128)
        return;

    if (is_released) {
        if (code == 42 || code == 54)
            shift_pressed = false;
        if (code == 29)
            ctrl_pressed = false;
        if (code == 56)
            alt_pressed = false;

        Event e;
        e.type = EVENT_KEY_RELEASE;
        e.key = lowercase[code];
        if (!dbg_ignore_kbd_push)
            event_push(e);

    } else {
        if (code == 42 || code == 54)
            shift_pressed = true;
        if (code == 29)
            ctrl_pressed = true;
        if (code == 56)
            alt_pressed = true;
        if (code == 58)
            caps_lock = !caps_lock;

        uint32_t ascii = 0;

        bool is_letter
            = (code >= 0x10 && code <= 0x19) || (code >= 0x1E && code <= 0x26) || (code >= 0x2C && code <= 0x32);

        if (is_letter) {
            if (shift_pressed ^ caps_lock) {
                ascii = uppercase[code];
            } else {
                ascii = lowercase[code];
            }
        } else {
            if (shift_pressed) {
                ascii = uppercase[code];
            } else {
                ascii = lowercase[code];
            }
        }

        if (ascii == 0 || ascii > 0xFF) {
            ascii = lowercase[code];
        }

        Event e;
        e.type = EVENT_KEY_PRESS;
        e.key = ascii;
        if (dbg_ignore_kbd_push) {
            dbg_kbd_ignored_count++;
        } else {
            event_push(e);
        }
    }
}

void keyboard_handler(struct InterruptRegisters *regs) {
    dbg_kbd_irq_count++;
    (void)regs;
    uint8_t scan_code = in_port_B(0x60);
    process_scancode(scan_code);
}

void keyboard_poll() {
    if (in_port_B(0x64) & 1) {
        uint8_t scan_code = in_port_B(0x60);
        dbg_kbd_irq_count++;
        process_scancode(scan_code);
    }
}

void keyboard_init() {
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    caps_lock = false;
    irq_install_handler(1, keyboard_handler);
}