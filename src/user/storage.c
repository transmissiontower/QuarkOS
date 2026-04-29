// SPDX-License-Identifier: MIT
/*
 * src/user/storage.c - Legacy FAT file read server using direct file system access
 */
#include "kernel/ipc.h"
#include "user/lib/libapi.h"
void main()
{
#if defined(__aarch64__)
    { register long x0 asm("x0") = 15; register long x1 asm("x1") = 0;
      register long x2 asm("x2") = 0; register long x3 asm("x3") = 0;
      asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory"); }
#elif defined(__arm__)
    { register int r0 asm("r0") = 15;
      asm volatile("svc #0" : "+r"(r0) :: "memory"); }
#else
    asm volatile("int $0x80" : : "a"(15));
#endif
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
