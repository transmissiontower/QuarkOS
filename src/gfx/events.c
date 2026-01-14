#include "gfx/events.h"

#define MAX_EVENTS 256

static Event event_queue[MAX_EVENTS];
static volatile int head = 0;
static volatile int tail = 0;

volatile int dbg_kbd_irq_count = 0;
volatile int dbg_mouse_irq_count = 0;
volatile int dbg_push_count = 0;
volatile int dbg_pop_count = 0;
volatile int dbg_handle_key_count = 0;
volatile int dbg_ignore_kbd_push = 0;
volatile int dbg_kbd_ignored_count = 0;

static inline uint32_t save_flags_and_cli(void) {
    uint32_t flags;
    asm volatile("pushf; pop %0" : "=r"(flags) : : "memory");
    asm volatile("cli" : : : "memory");
    return flags;
}

static inline void restore_flags(uint32_t flags) {
    asm volatile("push %0; popf" : : "r"(flags) : "memory");
}

void events_init() {
    head = 0;
    tail = 0;
}

void event_push(Event e) {
    uint32_t flags = save_flags_and_cli();
    int next_head = (head + 1) % MAX_EVENTS;

    if (next_head != tail) {
        event_queue[head] = e;
        head = next_head;
        dbg_push_count++;
    }
    restore_flags(flags);
}

int event_pop(Event *e) {
    uint32_t flags = save_flags_and_cli();
    if (tail == head) {
        restore_flags(flags);
        return 0;
    }

    *e = event_queue[tail];
    tail = (tail + 1) % MAX_EVENTS;
    dbg_pop_count++;
    restore_flags(flags);
    return 1;
}