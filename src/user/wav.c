// SPDX-License-Identifier: MIT
/*
 * src/user/wav.c - WAV audio file player using storage server streaming
 */
#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/path.h"
#define MSG_WAV_PLAY 400
#define CHUNK_SIZE (16 * 1024)
static int storage_pid = -1;
static uint32_t storage_open(const char *path) {
    uint32_t dir_cluster = 0;
    char short_name[13];
    if (qk_path_resolve(path, &dir_cluster, short_name) != 0)
        return 0;
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char *)req.payload, short_name, 12);
    qk_sys_ipc_send(storage_pid, &req);
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid)
            break;
    }
    if (resp.type != MSG_FILE_SIZE)
        return 0;
    return resp.data1;
}
static uint32_t storage_read(uint8_t *buf, uint32_t offset, uint32_t len) {
    uint32_t got = 0;
    while (got < len) {
        uint32_t chunk = len - got;
        if (chunk > 120)
            chunk = 120;
        message_t req, resp;
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = offset + got;
        req.data2 = chunk;
        qk_sys_ipc_send(storage_pid, &req);
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
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;
static int parse_wav(wav_info_t *info, uint32_t file_size) {
    uint8_t hdr[12];
    if (storage_read(hdr, 0, 12) < 12)
        return -1;
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F')
        return -1;
    if (hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E')
        return -1;
    uint32_t pos = 12;
    int got_fmt = 0;
    int got_data = 0;
    while (pos + 8 <= file_size && !(got_fmt && got_data)) {
        uint8_t chunk_hdr[8];
        if (storage_read(chunk_hdr, pos, 8) < 8)
            break;
        uint32_t chunk_size =
            (uint32_t)(chunk_hdr[4] | ((uint32_t)chunk_hdr[5] << 8) |
                       ((uint32_t)chunk_hdr[6] << 16) | ((uint32_t)chunk_hdr[7] << 24));
        if (chunk_hdr[0] == 'f' && chunk_hdr[1] == 'm' && chunk_hdr[2] == 't' &&
            chunk_hdr[3] == ' ') {
            uint8_t fmt[16];
            if (chunk_size < 16 || storage_read(fmt, pos + 8, 16) < 16)
                return -1;
            uint16_t audio_fmt = (uint16_t)(fmt[0] | ((uint16_t)fmt[1] << 8));
            if (audio_fmt != 1)
                return -1;
            info->channels = (uint16_t)(fmt[2] | ((uint16_t)fmt[3] << 8));
            info->sample_rate = (uint32_t)(fmt[4] | ((uint32_t)fmt[5] << 8) |
                                           ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24));
            info->bits_per_sample = (uint16_t)(fmt[14] | ((uint16_t)fmt[15] << 8));
            got_fmt = 1;
        } else if (chunk_hdr[0] == 'd' && chunk_hdr[1] == 'a' && chunk_hdr[2] == 't' &&
                   chunk_hdr[3] == 'a') {
            info->data_offset = pos + 8;
            info->data_size = chunk_size;
            got_data = 1;
        }
        pos += 8 + chunk_size;
        if (chunk_size & 1)
            pos++;
    }
    if (!got_fmt || !got_data)
        return -1;
    if (info->bits_per_sample != 8 && info->bits_per_sample != 16)
        return -1;
    if (info->channels < 1 || info->channels > 2)
        return -1;
    return 0;
}
static int shell_pid = -1;
static void wav_error(const char *reason) {
    if (shell_pid < 0)
        return;
    message_t err;
    memset(&err, 0, sizeof(err));
    err.type = MSG_ERROR;
    strncpy((char *)err.payload, reason, 127);
    qk_sys_ipc_send(shell_pid, &err);
}
int main(void) {
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage_pid < 0)
        return 1;
    char filename[128];
    while (1) {
        message_t msg;
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WAV_PLAY) {
            memcpy(filename, msg.payload, 127);
            filename[127] = '\0';
            shell_pid = msg.sender_pid;
            message_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.type = MSG_OK;
            qk_sys_ipc_send(shell_pid, &ack);
            break;
        }
    }
    qk_set_proc_name(qk_getpid(), "wav");
    uint32_t file_size = storage_open(filename);
    if (file_size == 0) {
        wav_error("file not found");
        return 1;
    }
    wav_info_t info;
    memset(&info, 0, sizeof(info));
    if (parse_wav(&info, file_size) != 0) {
        wav_error("bad WAV format");
        return 1;
    }
    if (info.sample_rate < 4000 || info.sample_rate > 44100 || info.data_size == 0) {
        wav_error("unsupported format");
        return 1;
    }
    uint8_t *buf = (uint8_t *)malloc(CHUNK_SIZE);
    if (!buf) {
        wav_error("out of memory");
        return 1;
    }
    uint32_t offset = info.data_offset;
    uint32_t remaining = info.data_size;
    while (remaining > 0) {
        uint32_t want = remaining < (uint32_t)CHUNK_SIZE ? remaining : (uint32_t)CHUNK_SIZE;
        uint32_t frame = (uint32_t)(info.bits_per_sample / 8) * info.channels;
        if (frame > 1)
            want = (want / frame) * frame;
        if (want == 0)
            break;
        uint32_t got = storage_read(buf, offset, want);
        if (got == 0)
            break;
        while (!qk_sb16_ready())
            qk_yield();
        if (qk_sb16_play(buf, got, info.sample_rate, (uint8_t)info.channels,
                         (uint8_t)info.bits_per_sample) != 0) {
            wav_error("SB16 not ready");
            break;
        }
        offset += got;
        remaining -= got;
    }
    while (!qk_sb16_ready())
        qk_yield();
    free(buf);
    qk_sys_exit(0);
    return 0;
}
