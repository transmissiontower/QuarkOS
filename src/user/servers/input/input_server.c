#include "abi/events.h"
#include "abi/ipc.h"
#include "stdbool.h"
#include "user/libc/libapi.h"
#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define ST_OUTPUT_FULL 0x01
#define ST_FROM_MOUSE 0x20
#define IRQ_KEYBOARD 1
#define IRQ_MOUSE 12
#define MOUSE_SPEED_MULTIPLIER 2
#define BUTTON_QUEUE_CAP 32
#define KEY_MOD_SHIFT 0x01
#define KEY_MOD_CTRL 0x02
#define KEY_MOD_ALT 0x04
#define KEY_MOD_CAPS 0x08
typedef struct {
    EventType type;
    uint8_t button;
    int x;
    int y;
    int scroll;
} mouse_event_t;
static int screen_w = 1920;
static int screen_h = 1080;
static int mouse_x = 1920 / 2;
static int mouse_y = 1080 / 2;
static int mouse_cycle = 0;
static uint8_t mouse_pkt[4];
static uint8_t mouse_buttons = 0;
static mouse_event_t button_queue[BUTTON_QUEUE_CAP];
static int button_head = 0;
static int button_tail = 0;
static bool move_pending = false;
static mouse_event_t move_event;
static void clamp_mouse_position(void) {
    if (screen_w <= 0) {
        screen_w = 1;
    }
    if (screen_h <= 0) {
        screen_h = 1;
    }
    if (mouse_x < 0)
        mouse_x = 0;
    if (mouse_y < 0)
        mouse_y = 0;
    if (mouse_x >= screen_w)
        mouse_x = screen_w - 1;
    if (mouse_y >= screen_h)
        mouse_y = screen_h - 1;
}
static void init_mouse_state(void) {
    fb_info_t fb;
    if (qk_map_framebuffer(&fb) == 0) {
        if ((int)fb.width > 0)
            screen_w = (int)fb.width;
        if ((int)fb.height > 0)
            screen_h = (int)fb.height;
    }
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    clamp_mouse_position();
}
static inline bool button_queue_empty(void) {
    return button_head == button_tail;
}
static int button_queue_next(int idx) {
    return (idx + 1) % BUTTON_QUEUE_CAP;
}
static void queue_button_event(EventType type, uint8_t button) {
    int next_tail = button_queue_next(button_tail);
    if (next_tail == button_head) {
        button_head = button_queue_next(button_head);
    }
    button_queue[button_tail] =
        (mouse_event_t){.type = type, .button = button, .x = mouse_x, .y = mouse_y};
    button_tail = next_tail;
}
static void queue_mouse_move(uint8_t buttons) {
    move_event.type = EVENT_MOUSE_MOVE;
    move_event.button = buttons;
    move_event.x = mouse_x;
    move_event.y = mouse_y;
    move_pending = true;
}
static void deliver_mouse_event(int comp_pid, mouse_event_t *evt) {
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    msg.type = MSG_MOUSE_EVENT;
    msg.data1 = evt->type;
    msg.data2 = evt->x;
    msg.data3 = evt->y;
    Event *payload = (Event *)msg.payload;
    payload->type = evt->type;
    payload->x = evt->x;
    payload->y = evt->y;
    payload->button = evt->button;
    payload->scroll = evt->scroll;
    (void)qk_sys_ipc_send(comp_pid, &msg);
}
static void flush_mouse_events(int comp_pid) {
    while (!button_queue_empty()) {
        mouse_event_t evt = button_queue[button_head];
        button_head = button_queue_next(button_head);
        deliver_mouse_event(comp_pid, &evt);
    }
    if (move_pending) {
        deliver_mouse_event(comp_pid, &move_event);
        move_pending = false;
    }
}
static bool shift_held = false;
static bool ctrl_held = false;
static bool alt_held = false;
static bool caps_lock = false;
static bool e0_pending = false;
static void handle_keyboard(int comp_pid, uint8_t scancode) {
    if (scancode == 0xE0) {
        e0_pending = true;
        return;
    }
    bool release = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    bool extended = e0_pending;
    e0_pending = false;
    if (!extended) {
        if (code == 0x2A || code == 0x36) {
            shift_held = !release;
        } else if (code == 0x1D) {
            ctrl_held = !release;
        } else if (code == 0x38) {
            alt_held = !release;
        } else if (code == 0x3A && !release) {
            caps_lock = !caps_lock;
        }
    } else {
        if (code == 0x1D)
            ctrl_held = !release;
        if (code == 0x38)
            alt_held = !release;
    }
    uint32_t mods = 0;
    if (shift_held)
        mods |= KEY_MOD_SHIFT;
    if (ctrl_held)
        mods |= KEY_MOD_CTRL;
    if (alt_held)
        mods |= KEY_MOD_ALT;
    if (caps_lock)
        mods |= KEY_MOD_CAPS;
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    msg.type = MSG_KEY_EVENT;
    msg.data1 = (uint32_t)code | (release ? 0x80U : 0) | (extended ? 0x100U : 0);
    msg.data2 = mods;
    Event *ev = (Event *)msg.payload;
    ev->type = release ? EVENT_KEY_RELEASE : EVENT_KEY_PRESS;
    ev->key = (uint32_t)code | (extended ? 0x100U : 0);
    (void)qk_sys_ipc_send(comp_pid, &msg);
}
static void queue_scroll_event(int scroll) {
    int next_tail = button_queue_next(button_tail);
    if (next_tail == button_head)
        button_head = button_queue_next(button_head);
    button_queue[button_tail] = (mouse_event_t){
        .type = EVENT_MOUSE_SCROLL, .button = 0, .x = mouse_x, .y = mouse_y, .scroll = scroll};
    button_tail = next_tail;
}
static void handle_mouse_byte(uint8_t data) {
    switch (mouse_cycle) {
    case 0:
        if (!(data & 0x08))
            return;
        mouse_pkt[0] = data;
        mouse_cycle = 1;
        break;
    case 1:
        mouse_pkt[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_pkt[2] = data;
        mouse_cycle = 3;
        break;
    case 3: {
        mouse_pkt[3] = data;
        mouse_cycle = 0;
        int dx = (int8_t)mouse_pkt[1];
        int dy = (int8_t)mouse_pkt[2];
        dx *= MOUSE_SPEED_MULTIPLIER;
        dy *= MOUSE_SPEED_MULTIPLIER;
        int prev_x = mouse_x;
        int prev_y = mouse_y;
        mouse_x += dx;
        mouse_y -= dy;
        clamp_mouse_position();
        uint8_t buttons = mouse_pkt[0] & 0x07;
        if (mouse_x != prev_x || mouse_y != prev_y) {
            queue_mouse_move(buttons);
        }
        uint8_t changed = buttons ^ mouse_buttons;
        if (changed) {
            if (changed & 0x01) {
                queue_button_event((buttons & 0x01) ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP,
                                   MOUSE_BTN_LEFT);
            }
            if (changed & 0x02) {
                queue_button_event((buttons & 0x02) ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP,
                                   MOUSE_BTN_RIGHT);
            }
            if (changed & 0x04) {
                queue_button_event((buttons & 0x04) ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP,
                                   MOUSE_BTN_MIDDLE);
            }
            mouse_buttons = buttons;
        }
        uint8_t raw = mouse_pkt[3] & 0x0F;
        int scroll = (raw & 0x08) ? (int)raw - 16 : (int)raw;
        if (scroll != 0)
            queue_scroll_event(scroll);
        break;
    }
    }
}
static int resolve_compositor_pid(void) {
    int pid;
    do {
        pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
        if (pid <= 0)
            qk_sleep(100);
    } while (pid <= 0);
    return pid;
}
void main() {
    init_mouse_state();
    while (qk_input_compat_read(PS2_STATUS) & ST_OUTPUT_FULL)
        qk_input_compat_read(PS2_DATA);
    int comp_pid = resolve_compositor_pid();
    int comp_check_counter = 0;
    int idle_polls = 0;
    while (1) {
        uint8_t status = qk_input_compat_read(PS2_STATUS);
        if (status & ST_OUTPUT_FULL) {
            idle_polls = 0;
            int bytes_this_batch = 0;
            do {
                uint8_t data = qk_input_compat_read(PS2_DATA);
                if (status & ST_FROM_MOUSE)
                    handle_mouse_byte(data);
                else
                    handle_keyboard(comp_pid, data);

                if (++bytes_this_batch >= 64) {
                    flush_mouse_events(comp_pid);
                    qk_yield();
                    break;
                }
                status = qk_input_compat_read(PS2_STATUS);
            } while (status & ST_OUTPUT_FULL);
        } else {
            flush_mouse_events(comp_pid);
            if (++comp_check_counter >= 256) {
                comp_check_counter = 0;
                int new_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
                if (new_pid > 0 && new_pid != comp_pid)
                    comp_pid = new_pid;
                else if (new_pid <= 0)
                    comp_pid = resolve_compositor_pid();
            }

            if (++idle_polls >= 32) {
                idle_polls = 0;
                qk_yield();
            }
        }
        flush_mouse_events(comp_pid);
    }
}
