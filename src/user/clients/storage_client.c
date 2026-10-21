#include "user/clients/storage_client.h"
#include "stdlib.h"
#include "user/libc/libapi.h"
void *storage_read_file(int storage_pid, const char *path, uint32_t *size_out) {
    if (size_out)
        *size_out = 0;
    if (storage_pid < 0 || !path)
        return 0;
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = 0xFFFFu;
    strncpy((char *)req.payload, path, sizeof(req.payload) - 1);
    if (qk_ipc_send_recv(storage_pid, &req, &resp) != 0 || resp.type != MSG_FILE_SIZE ||
        resp.data1 == 0)
        return 0;
    uint32_t size = resp.data1;
    uint8_t *data = (uint8_t *)malloc(size + 1);
    if (!data)
        return 0;
    uint32_t offset = 0;
    while (offset < size) {
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = offset;
        req.data2 = sizeof(resp.payload);
        if (qk_ipc_send_recv(storage_pid, &req, &resp) != 0 || resp.type != MSG_FILE_DATA ||
            resp.data1 == 0)
            break;
        uint32_t chunk = resp.data1;
        if (chunk > size - offset)
            chunk = size - offset;
        memcpy(data + offset, resp.payload, chunk);
        offset += chunk;
    }
    if (offset != size) {
        free(data);
        return 0;
    }
    data[size] = 0;
    if (size_out)
        *size_out = size;
    return data;
}

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
