// SPDX-License-Identifier: MIT
/*
 * src/gfx/events.h - Input event types and queue interface for keyboard and mouse
 */
#ifndef EVENTS_H
#define EVENTS_H
#include "lib/stdint.h"
typedef enum {
    EVENT_NONE = 0,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_SCROLL,
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
    int32_t scroll;
} Event;
#endif
