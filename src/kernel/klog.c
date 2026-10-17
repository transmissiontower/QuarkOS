// SPDX-License-Identifier: MIT
#include "kernel/klog.h"

#include "arch/aarch64/platform.h"

#define KLOG_RING_SIZE (32 * 1024)
static char klog_ring[KLOG_RING_SIZE];
static uint32_t klog_head = 0;
static uint32_t klog_tail = 0;

static log_level_t uart_level = LOG_INFO;
static log_level_t ring_level = LOG_DEBUG;

static const char *level_names[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT "};

static const char *subsys_names[] = {"BOOT", "SCHED", "VMM",     "PMM", "IPC", "INPUT",
                                     "BLK",  "GIC",   "SYSCALL", "GPU", "FS",  "PROC"};

extern uint64_t get_uptime_ms(void);

void klog_init(void) {
    klog_head = 0;
    klog_tail = 0;
}

void klog_set_uart_level(log_level_t level) {
    uart_level = level;
}

void klog_set_ring_level(log_level_t level) {
    ring_level = level;
}

static void ring_putc(char c) {
    klog_ring[klog_tail] = c;
    klog_tail = (klog_tail + 1) % KLOG_RING_SIZE;
    if (klog_tail == klog_head)
        klog_head = (klog_head + 1) % KLOG_RING_SIZE;
}

static void ring_puts(const char *s) {
    while (*s)
        ring_putc(*s++);
}

static void emit_uint(void (*put)(char), uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        put('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        put(buf[i]);
}

static void emit_hex32(void (*put)(char), uint32_t val) {
    put('0');
    put('x');
    for (int s = 28; s >= 0; s -= 4) {
        int n = (val >> s) & 0xF;
        put(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static void emit_hex64(void (*put)(char), uint64_t val) {
    put('0');
    put('x');
    for (int s = 60; s >= 0; s -= 4) {
        int n = (int)((val >> s) & 0xF);
        put(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static void klog_vformat(void (*put)(char), const char *fmt, __builtin_va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            put(*fmt++);
            continue;
        }
        fmt++;
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s)
                put(*s++);
            break;
        }
        case 'd': {
            int32_t v = __builtin_va_arg(ap, int32_t);
            if (v < 0) {
                put('-');
                v = -v;
            }
            emit_uint(put, (uint32_t)v);
            break;
        }
        case 'u': {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            emit_uint(put, v);
            break;
        }
        case 'x': {
            if (is_long) {
                uint64_t v = __builtin_va_arg(ap, uint64_t);
                emit_hex64(put, v);
            } else {
                uint32_t v = __builtin_va_arg(ap, uint32_t);
                emit_hex32(put, v);
            }
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)__builtin_va_arg(ap, void *);
            emit_hex64(put, (uint64_t)v);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            put(c);
            break;
        }
        case '%':
            put('%');
            break;
        default:
            put('%');
            put(*fmt);
            break;
        }
        fmt++;
    }
}

static void uart_putc_wrapper(char c) {
    if (c == '\n')
        arm_uart_putc('\r');
    arm_uart_putc(c);
}

void klog(log_level_t level, int subsys, const char *fmt, ...) {
    if (level < ring_level && level < uart_level)
        return;

    uint64_t flags = arm_irq_save();

    uint64_t ms = get_uptime_ms();
    uint32_t secs = (uint32_t)(ms / 1000);
    uint32_t frac = (uint32_t)(ms % 1000);

    __builtin_va_list ap;

    if (level >= ring_level) {
        ring_putc('[');

        char tbuf[12];
        int ti = 0;
        uint32_t sv = secs;
        if (sv == 0)
            tbuf[ti++] = '0';
        else {
            char tmp[10];
            int j = 0;
            while (sv) {
                tmp[j++] = '0' + (sv % 10);
                sv /= 10;
            }
            while (--j >= 0)
                tbuf[ti++] = tmp[j];
        }
        tbuf[ti++] = '.';
        tbuf[ti++] = '0' + (frac / 100);
        tbuf[ti++] = '0' + ((frac / 10) % 10);
        tbuf[ti++] = '0' + (frac % 10);
        tbuf[ti] = 0;
        ring_puts(tbuf);
        ring_puts("] ");
        ring_puts(level_names[level]);
        ring_putc(' ');
        if (subsys >= 0 && subsys <= 11)
            ring_puts(subsys_names[subsys]);
        ring_puts(": ");

        __builtin_va_start(ap, fmt);
        klog_vformat(ring_putc, fmt, ap);
        __builtin_va_end(ap);
        ring_putc('\n');
    }

    if (level >= uart_level) {
        uart_putc_wrapper('[');
        emit_uint(uart_putc_wrapper, secs);
        uart_putc_wrapper('.');
        emit_uint(uart_putc_wrapper, frac / 100);
        emit_uint(uart_putc_wrapper, (frac / 10) % 10);
        emit_uint(uart_putc_wrapper, frac % 10);
        uart_putc_wrapper(']');
        uart_putc_wrapper(' ');
        const char *ln = level_names[level];
        while (*ln)
            uart_putc_wrapper(*ln++);
        uart_putc_wrapper(' ');
        if (subsys >= 0 && subsys <= 11) {
            const char *sn = subsys_names[subsys];
            while (*sn)
                uart_putc_wrapper(*sn++);
        }
        uart_putc_wrapper(':');
        uart_putc_wrapper(' ');

        __builtin_va_start(ap, fmt);
        klog_vformat(uart_putc_wrapper, fmt, ap);
        __builtin_va_end(ap);
        uart_putc_wrapper('\n');
    }

    arm_irq_restore(flags);
}

void klog_hex32(uint32_t val) {
    for (int s = 28; s >= 0; s -= 4) {
        int n = (val >> s) & 0xF;
        arm_uart_putc(n < 10 ? '0' + n : 'A' + n - 10);
    }
}

void klog_hex64(uint64_t val) {
    klog_hex32((uint32_t)(val >> 32));
    klog_hex32((uint32_t)val);
}

int klog_read(char *buf, uint32_t max_len) {
    uint32_t copied = 0;
    uint64_t flags = arm_irq_save();
    while (klog_head != klog_tail && copied < max_len) {
        buf[copied++] = klog_ring[klog_head];
        klog_head = (klog_head + 1) % KLOG_RING_SIZE;
    }
    arm_irq_restore(flags);
    return (int)copied;
}
