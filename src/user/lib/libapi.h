#ifndef LIBAPI_H
#define LIBAPI_H
#include "gfx/events.h"
#include "kernel/ipc.h"
#include "lib/stdint.h"
typedef struct {
    uint32_t width, height, pitch, bpp;
    void *buffer;
} fb_info_t;
typedef struct {
    uint32_t start, end, flags;
    char name[16];
} vmap_region_t;
typedef struct {
    uint8_t second, minute, hour, day, month;
    uint32_t year;
} qk_time_t;
typedef struct {
    int pid;
    int state;
    char name[16];
} process_info_t;
typedef enum { QK_SERVER_STORAGE = 0, QK_SERVER_COMPOSITOR = 1, QK_SERVER_INPUT = 2 } qk_server_type_t;
void qk_sys_exit(int code);
void qk_sleep(uint32_t ms);
void qk_yield(void);
void qk_wait_irq(int irq);
uint32_t qk_get_uptime();
int qk_spawn(void *buffer, uint32_t size);
int qk_get_server_pid(qk_server_type_t type);
int qk_map_framebuffer(fb_info_t *info);
int qk_sys_ipc_send(int dest_pid, message_t *msg);
int qk_ipc_recv(message_t *msg);
int qk_ipc_recv_nb(message_t *msg);
int qk_ipc_send_recv(int dest_pid, message_t *msg, message_t *reply);
int qk_sbrk(uint32_t size);
int qk_map_wallpaper(void **ptr);
int qk_map_font_atlas(void **ptr);
int qk_map_icons_atlas(void **ptr);
int qk_get_vmap(vmap_region_t *out);
int qk_sys_kill(int pid);
int qk_get_procs(process_info_t *out, int max);
int qk_set_proc_name(int pid, const char *name);
int qk_get_time(qk_time_t *t);
void qk_print_at(int x, int y, uint32_t color, const char *str);
void sys_window_close(int id);
void qk_power_shutdown(void);
void qk_power_reboot(void);
void qk_trigger_panic(const char *msg);
void qk_pcspk_play(uint32_t freq);
void qk_pcspk_stop(void);
void qk_pcspk_pwm_play(const uint8_t *buf, uint32_t len, uint32_t rate);
int  qk_pcspk_pwm_ready(void);
uint8_t qk_hw_inb(uint16_t port);
void qk_hw_outb(uint16_t port, uint8_t val);
uint32_t qk_hw_inl(uint16_t port);
void qk_hw_outl(uint16_t port, uint32_t val);
int  qk_sb16_init(void);
int  qk_sb16_play(const void *buf, uint32_t len, uint32_t rate,
                  uint8_t channels, uint8_t bits);
int  qk_sb16_ready(void);
int qk_ata_read_sectors(uint32_t lba, uint32_t count, void *buf);
int      qk_virt_to_phys_list(uint32_t virt, uint32_t size, uint32_t *out);
uint32_t qk_map_phys_list(uint32_t *phys_pages, uint32_t count);
void    *qk_map_foreign_mem(int src_pid, uint32_t src_virt, uint32_t size);
void    *qk_win_map_buffer(int comp_pid, int win_id, int *w_out, int *h_out);
void     qk_win_dirty(int comp_pid);
uint16_t qk_inw(uint16_t port);
void qk_outw(uint16_t port, uint16_t val);
int qk_get_stats(uint32_t *used, uint32_t *total, uint32_t *cpu);
void qk_blit_pixels(int comp_pid, int x, int y, uint32_t *pixels, int count);
void qk_compositor_suspend(int comp_pid);
void qk_compositor_resume(int comp_pid);
void qk_input_redirect(int target_pid);
void qk_input_restore(void);
int  qk_getpid(void);
int      qk_waitpid(int pid);
int      qk_pipe_create(const char *name);
int      qk_pipe_open(const char *name);
int      qk_pipe_write(int pipe_id, const uint8_t *buf, uint32_t len);
int      qk_pipe_read(int pipe_id, uint8_t *buf, uint32_t len);
int      qk_pipe_close(int pipe_id);
int      qk_set_alarm(uint32_t ms);
int      qk_setenv(const char *kv);
int      qk_getenv(const char *key, char *out, uint32_t maxlen);
int      qk_signal_send(int pid, int signum);
void *malloc(uint32_t size);
void *realloc(void *ptr, uint32_t new_size);
void free(void *ptr);
void *memcpy(void *dest, const void *src, uint32_t n);
void *memmove(void *dest, const void *src, uint32_t n);
void *memset(void *s, int c, uint32_t n);
int memcmp(const void *s1, const void *s2, uint32_t n);
void memcpy32(void *dest, const void *src, uint32_t count_bytes);
void memset32(void *dest, uint32_t val, uint32_t count_dwords);
#endif
