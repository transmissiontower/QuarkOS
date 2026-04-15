#include "user/lib/libapi.h"
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
uint8_t read_cmos(uint8_t reg)
{
    qk_hw_outb(CMOS_ADDR, reg);
    return qk_hw_inb(CMOS_DATA);
}
int main()
{
    message_t msg;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == 0x20) {
            message_t resp = {.sender_pid = 0, .type = 0x21};
            resp.payload[0] = read_cmos(0x04);
            resp.payload[1] = read_cmos(0x02);
            resp.payload[2] = read_cmos(0x00);
            qk_sys_ipc_send(msg.sender_pid, &resp);
        }
    }
    return 0;
}
