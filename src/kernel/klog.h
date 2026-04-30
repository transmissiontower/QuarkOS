// SPDX-License-Identifier: MIT
/*
 * src/kernel/klog.h - Kernel log levels, subsystem tags, and logging macros
 */
#ifndef KLOG_H
#define KLOG_H

#include "lib/stdint.h"

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_CRIT = 5
} log_level_t;

#define KSUB_BOOT 0
#define KSUB_SCHED 1
#define KSUB_VMM 2
#define KSUB_PMM 3
#define KSUB_IPC 4
#define KSUB_INPUT 5
#define KSUB_BLK 6
#define KSUB_GIC 7
#define KSUB_SYSCALL 8
#define KSUB_GPU 9
#define KSUB_FS 10
#define KSUB_PROC 11

void klog_init(void);
void klog(log_level_t level, int subsys, const char *fmt, ...);
void klog_set_uart_level(log_level_t level);
void klog_set_ring_level(log_level_t level);

int klog_read(char *buf, uint32_t max_len);

void klog_hex32(uint32_t val);
void klog_hex64(uint64_t val);

#define KLOG_BOOT(lvl, fmt, ...) klog(lvl, KSUB_BOOT, fmt, ##__VA_ARGS__)
#define KLOG_SCHED(lvl, fmt, ...) klog(lvl, KSUB_SCHED, fmt, ##__VA_ARGS__)
#define KLOG_VMM(lvl, fmt, ...) klog(lvl, KSUB_VMM, fmt, ##__VA_ARGS__)
#define KLOG_PMM(lvl, fmt, ...) klog(lvl, KSUB_PMM, fmt, ##__VA_ARGS__)
#define KLOG_IPC(lvl, fmt, ...) klog(lvl, KSUB_IPC, fmt, ##__VA_ARGS__)
#define KLOG_INPUT(lvl, fmt, ...) klog(lvl, KSUB_INPUT, fmt, ##__VA_ARGS__)
#define KLOG_BLK(lvl, fmt, ...) klog(lvl, KSUB_BLK, fmt, ##__VA_ARGS__)
#define KLOG_GIC(lvl, fmt, ...) klog(lvl, KSUB_GIC, fmt, ##__VA_ARGS__)
#define KLOG_SYS(lvl, fmt, ...) klog(lvl, KSUB_SYSCALL, fmt, ##__VA_ARGS__)
#define KLOG_PROC(lvl, fmt, ...) klog(lvl, KSUB_PROC, fmt, ##__VA_ARGS__)

#endif
