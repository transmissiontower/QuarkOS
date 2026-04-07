#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/storage_client.h"

static int storage_pid = -1;

static uint32_t file_read(uint8_t *buf, uint32_t offset, uint32_t len)
{
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

static uint32_t file_open(const char *name, uint16_t dir_cluster)
{
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
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;

static int parse_wav_header(wav_info_t *info)
{
    uint8_t hdr[44];
    if (file_read(hdr, 0, 44) < 44)
        return -1;

    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F')
        return -1;
    if (hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E')
        return -1;

    info->channels        = (uint16_t)(hdr[22] | (hdr[23] << 8));
    info->sample_rate     = (uint32_t)(hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24));
    info->bits_per_sample = (uint16_t)(hdr[34] | (hdr[35] << 8));

    if (hdr[36] == 'd' && hdr[37] == 'a' && hdr[38] == 't' && hdr[39] == 'a') {
        info->data_offset = 44;
        info->data_size   = (uint32_t)(hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | (hdr[43] << 24));
    } else {
        return -1;
    }

    if (info->bits_per_sample != 8 || info->channels != 1)
        return -1;

    return 0;
}

#define PLAY_BUF_SIZE 8192
#define PLAY_SLOTS    3

static uint8_t play_buf[PLAY_SLOTS][PLAY_BUF_SIZE];
static uint32_t play_len[PLAY_SLOTS]; 

int main()
{
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage_pid < 0)
        return 1;

    uint32_t file_size = file_open("TEST.WAV", 0);
    if (file_size == 0)
        return 1;

    wav_info_t info;
    if (parse_wav_header(&info) != 0)
        return 1;

    if (!qk_sb16_ready())
        return 1;

    uint32_t offset    = info.data_offset;
    uint32_t remaining = info.data_size;

    int primed = 0;
    for (int s = 0; s < 2 && remaining > 0; s++) {
        uint32_t chunk = remaining < PLAY_BUF_SIZE ? remaining : PLAY_BUF_SIZE;
        uint32_t got   = file_read(play_buf[s], offset, chunk);
        if (got == 0)
            break;
        play_len[s] = got;
        offset      += got;
        remaining   -= got;
        primed++;
    }
    if (primed == 0) {
        qk_sys_exit(0);
        return 0;
    }

    int play_slot = 0;

    qk_sb16_play(play_buf[play_slot], play_len[play_slot],
                 info.sample_rate, 1, 8);

    while (1) {
        int next_slot = (play_slot + 1) % PLAY_SLOTS;
        int fill_slot = (play_slot + 2) % PLAY_SLOTS;

        if (remaining > 0) {
            uint32_t chunk = remaining < PLAY_BUF_SIZE ? remaining : PLAY_BUF_SIZE;
            uint32_t got   = file_read(play_buf[fill_slot], offset, chunk);
            if (got > 0) {
                play_len[fill_slot] = got;
                offset    += got;
                remaining -= got;
            } else {
                play_len[fill_slot] = 0; 
            }
        } else {
            play_len[fill_slot] = 0;
        }

        while (!qk_sb16_ready())
            qk_yield();

        if (play_len[next_slot] == 0)
            break; 

        qk_sb16_play(play_buf[next_slot], play_len[next_slot],
                     info.sample_rate, 1, 8);
        play_slot = next_slot;
    }

    while (!qk_sb16_ready())
        qk_yield();

    qk_sys_exit(0);
    return 0;
}
