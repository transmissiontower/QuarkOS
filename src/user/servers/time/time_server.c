#include "user/libc/libapi.h"

static uint8_t get_hour(void) {
    qk_time_t t;
    qk_get_time(&t);
    return t.hour;
}
static uint8_t get_minute(void) {
    qk_time_t t;
    qk_get_time(&t);
    return t.minute;
}
static uint8_t get_second(void) {
    qk_time_t t;
    qk_get_time(&t);
    return t.second;
}

int main() {
    message_t msg;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == 0x20) {
            message_t resp = {.sender_pid = 0, .type = 0x21};
            resp.payload[0] = get_hour();
            resp.payload[1] = get_minute();
            resp.payload[2] = get_second();
            qk_sys_ipc_send(msg.sender_pid, &resp);
        }
    }
    return 0;
}
