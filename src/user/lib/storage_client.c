// SPDX-License-Identifier: MIT
/*
 * src/user/lib/storage_client.c - IPC client for saving files to disk via the storage server
 */
#include "user/lib/storage_client.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
static int wait_for_storage_reply(int storage_pid, message_t *out) {
    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid) {
            if (out)
                *out = resp;
            return resp.type;
        }
    }
}
int storage_send_file(int storage_pid, uint32_t dir_cluster, const char *filename,
                      const uint8_t *data, uint32_t size, int disk_idx) {
    if (storage_pid < 0 || !filename)
        return -1;
    if (size > 0 && !data)
        return -1;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE_SAVE_BEGIN;
    msg.data1 = dir_cluster;
    msg.data2 = size;
    msg.data3 = (uint32_t)disk_idx;
    strncpy((char *)msg.payload, filename, 63);
    qk_sys_ipc_send(storage_pid, &msg);
    if (wait_for_storage_reply(storage_pid, 0) != MSG_OK)
        return -1;
    uint32_t sent = 0;
    while (sent < size) {
        uint32_t chunk = size - sent;
        if (chunk > sizeof(msg.payload))
            chunk = sizeof(msg.payload);
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_FILE_SAVE_CHUNK;
        msg.data1 = chunk;
        memcpy(msg.payload, data + sent, chunk);
        qk_sys_ipc_send(storage_pid, &msg);
        if (wait_for_storage_reply(storage_pid, 0) != MSG_OK)
            return -1;
        sent += chunk;
    }
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE_SAVE_END;
    msg.data1 = size;
    qk_sys_ipc_send(storage_pid, &msg);
    return (wait_for_storage_reply(storage_pid, 0) == MSG_OK) ? 0 : -1;
}
