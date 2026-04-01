#ifndef EVENTS_H
#define EVENTS_H

#include "lib/stdint.h"

typedef enum {
    EVENT_NONE = 0,

    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE
} EventType;

#define MOUSE_BTN_LEFT 1
#define MOUSE_BTN_RIGHT 2
#define MOUSE_BTN_MIDDLE 3

typedef struct {
    EventType type;

    int32_t x;
    int32_t y;
    uint32_t key;
    uint8_t button;
} Event;

void events_init();
void event_push(Event e);
int event_pop(Event *e);

extern volatile int dbg_kbd_irq_count;
extern volatile int dbg_mouse_irq_count;
extern volatile int dbg_push_count;
extern volatile int dbg_pop_count;
extern volatile int dbg_handle_key_count;
extern volatile int dbg_ignore_kbd_push;
extern volatile int dbg_kbd_ignored_count;

#endif
