// SPDX-License-Identifier: MIT
/*
 * src/user/spkplay.c - PC speaker music file (.spk) player
 */
#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/storage_client.h"
#define SPK_MAGIC_0 'S'
#define SPK_MAGIC_1 'P'
#define SPK_MAGIC_2 'K'
#define SPK_MAGIC_3 'W'
#define SPK_HEADER_SIZE 12
#define PLAY_BUF_SIZE 4096
#define PLAY_SLOTS 3
static uint8_t play_buf[PLAY_SLOTS][PLAY_BUF_SIZE];
static uint32_t play_len[PLAY_SLOTS];
static int storage_pid = -1;
static uint32_t file_read(uint8_t *buf, uint32_t offset, uint32_t len) {
    uint32_t got = 0;
    while (got < len) {
        uint32_t chunk = len - got;
        if (chunk > 120)
            chunk = 120;
        message_t req;
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = offset + got;
        req.data2 = chunk;
        qk_sys_ipc_send(storage_pid, &req);
        message_t resp;
        while (1) {
            qk_ipc_recv(&resp);
            if (resp.sender_pid == storage_pid && resp.type == MSG_FILE_DATA)
                break;
        }
        if (resp.data1 == 0)
            break;
        memcpy(buf + got, resp.payload, resp.data1);
        got += resp.data1;
    }
    return got;
}
static uint32_t file_open(const char *name, uint32_t dir_cluster) {
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char *)req.payload, name, 12);
    qk_sys_ipc_send(storage_pid, &req);
    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid)
            break;
    }
    if (resp.type != MSG_FILE_SIZE)
        return 0;
    return resp.data1;
}
typedef struct {
    uint32_t sample_rate;
    uint32_t num_samples;
} spk_info_t;
static int parse_spk_header(spk_info_t *info) {
    uint8_t hdr[SPK_HEADER_SIZE];
    if (file_read(hdr, 0, SPK_HEADER_SIZE) < (uint32_t)SPK_HEADER_SIZE)
        return -1;
    if (hdr[0] != SPK_MAGIC_0 || hdr[1] != SPK_MAGIC_1 || hdr[2] != SPK_MAGIC_2 ||
        hdr[3] != SPK_MAGIC_3)
        return -1;
    info->sample_rate = (uint32_t)(hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24));
    info->num_samples = (uint32_t)(hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | (hdr[11] << 24));
    if (info->sample_rate == 0 || info->num_samples == 0)
        return -1;
    return 0;
}
int main() {
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage_pid < 0)
        return 1;
    uint32_t file_size = file_open("MUSIC.SPK", 0);
    if (file_size == 0)
        return 1;
    spk_info_t info;
    if (parse_spk_header(&info) != 0)
        return 1;
    uint32_t offset = SPK_HEADER_SIZE;
    uint32_t remaining = info.num_samples;
    int primed = 0;
    for (int s = 0; s < 2 && remaining > 0; s++) {
        uint32_t chunk = remaining < PLAY_BUF_SIZE ? remaining : PLAY_BUF_SIZE;
        uint32_t got = file_read(play_buf[s], offset, chunk);
        if (got == 0)
            break;
        play_len[s] = got;
        offset += got;
        remaining -= got;
        primed++;
    }
    if (primed == 0)
        return 1;
    int play_slot = 0;
    qk_pcspk_pwm_play(play_buf[play_slot], play_len[play_slot], info.sample_rate);
    while (1) {
        int next_slot = (play_slot + 1) % PLAY_SLOTS;
        int fill_slot = (play_slot + 2) % PLAY_SLOTS;
        if (remaining > 0) {
            uint32_t chunk = remaining < PLAY_BUF_SIZE ? remaining : PLAY_BUF_SIZE;
            uint32_t got = file_read(play_buf[fill_slot], offset, chunk);
            if (got > 0) {
                play_len[fill_slot] = got;
                offset += got;
                remaining -= got;
            } else {
                play_len[fill_slot] = 0;
            }
        } else {
            play_len[fill_slot] = 0;
        }
        while (!qk_pcspk_pwm_ready())
            qk_yield();
        if (play_len[next_slot] == 0)
            break;
        qk_pcspk_pwm_play(play_buf[next_slot], play_len[next_slot], info.sample_rate);
        play_slot = next_slot;
    }
    while (!qk_pcspk_pwm_ready())
        qk_yield();
    qk_sys_exit(0);
    return 0;
}
