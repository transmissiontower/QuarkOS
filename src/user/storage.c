#include "kernel/ipc.h"
#include "user/lib/libapi.h"
void main()
{
    asm volatile("int $0x80" : : "a"(15));
    fat_init_user();
    message_t msg;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_FILE_READ) {
            char *filename = (char *)msg.payload;
            uint32_t size;
            uint8_t *data = fat_read_file_user(filename, &size);
            message_t response;
            response.type = MSG_OK;
            response.data1 = (uint32_t)data;
            response.data2 = size;
            qk_sys_ipc_send(msg.sender_pid, &response);
        }
    }
}
