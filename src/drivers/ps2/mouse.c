#include "drivers/ps2/mouse.h"
#include "arch/x86/idt.h"
#include "drivers/GUI/framebuffer.h"
#include "gfx/events.h"
#include "util/util.h"

int32_t mouse_x = 0;
int32_t mouse_y = 0;
int8_t mouse_left = 0;
int8_t mouse_right = 0;
int8_t mouse_middle = 0;
uint8_t mouse_cycle = 0;
int8_t mouse_byte[3];

static uint8_t old_mouse_left = 0;
static uint8_t old_mouse_right = 0;

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((in_port_B(0x64) & 1) == 1)
                return;
        }
        return;
    } else {
        while (timeout--) {
            if ((in_port_B(0x64) & 2) == 0)
                return;
        }
        return;
    }
}

void mouse_write(uint8_t a_write) {
    mouse_wait(1);
    out_port_B(0x64, 0xD4);
    mouse_wait(1);
    out_port_B(0x60, a_write);
}

uint8_t mouse_read() {
    mouse_wait(0);
    return (uint8_t)in_port_B(0x60);
}

void mouse_handler(struct InterruptRegisters *regs) {
    dbg_mouse_irq_count++;
    (void)regs;

    uint8_t status = in_port_B(0x64);
    if (!(status & 0x20))
        return;

    int8_t in = in_port_B(0x60);

    switch (mouse_cycle) {
    case 0:
        if ((in & 0x08) == 0 || (in & 0xC0)) {
            mouse_cycle = 0;
            return;
        }

        mouse_byte[0] = in;
        mouse_cycle++;
        break;
    case 1:
        mouse_byte[1] = in;
        mouse_cycle++;
        break;
    case 2:
        mouse_byte[2] = in;

        int32_t prev_x = mouse_x;
        int32_t prev_y = mouse_y;

        mouse_x += mouse_byte[1];
        mouse_y -= mouse_byte[2];

        if (mouse_x < 0)
            mouse_x = 0;
        if (mouse_y < 0)
            mouse_y = 0;
        if (mouse_x >= (int32_t)fb_width)
            mouse_x = fb_width - 1;
        if (mouse_y >= (int32_t)fb_height)
            mouse_y = fb_height - 1;

        if (mouse_x != prev_x || mouse_y != prev_y) {
            Event e;
            e.type = EVENT_MOUSE_MOVE;
            e.x = mouse_x;
            e.y = mouse_y;
            event_push(e);
        }

        uint8_t cur_left = (mouse_byte[0] & 0x01) ? 1 : 0;
        uint8_t cur_right = (mouse_byte[0] & 0x02) ? 1 : 0;

        if (cur_left != old_mouse_left) {
            Event e;
            e.type = cur_left ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP;
            e.button = MOUSE_BTN_LEFT;
            e.x = mouse_x;
            e.y = mouse_y;
            event_push(e);
            old_mouse_left = cur_left;
            mouse_left = cur_left;
        }

        if (cur_right != old_mouse_right) {
            Event e;
            e.type = cur_right ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP;
            e.button = MOUSE_BTN_RIGHT;
            e.x = mouse_x;
            e.y = mouse_y;
            event_push(e);
            old_mouse_right = cur_right;
            mouse_right = cur_right;
        }

        mouse_cycle = 0;
        break;
    }
}

void mouse_init() {
    uint8_t status;

    mouse_wait(1);
    out_port_B(0x64, 0xA8);

    mouse_wait(1);
    out_port_B(0x64, 0x20);
    mouse_wait(0);
    status = (in_port_B(0x60) | 2);

    mouse_wait(1);
    out_port_B(0x64, 0x60);
    mouse_wait(1);
    out_port_B(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    irq_install_handler(12, mouse_handler);

    uint32_t dump_loops = 100;
    while ((in_port_B(0x64) & 0x01) && dump_loops > 0) {
        in_port_B(0x60);
        dump_loops--;
    }

    mouse_x = fb_width / 2;
    mouse_y = fb_height / 2;
}