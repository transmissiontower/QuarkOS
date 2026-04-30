// SPDX-License-Identifier: MIT
/*
 * src/user/lib/stdio.c - Standard I/O (printf, file operations, formatted output)
 */
#include "user/lib/stdio.h"
#include "user/lib/libapi.h"
#include "kernel/ipc.h"
#include "lib/stdlib.h"
static FILE _stderr_impl = {0, 0, -1, 0, 1};
static FILE _stdout_impl = {0, 0, -1, 0, 1};
static FILE _stdin_impl = {0, 0, -1, 0, 1};
FILE *stderr = &_stderr_impl;
FILE *stdout = &_stdout_impl;
FILE *stdin = &_stdin_impl;
static void _putc_buf(char *buf, uint32_t *pos, uint32_t max, char c) {
    if (*pos < max - 1)
        buf[(*pos)++] = c;
}
static void _puts_buf(char *buf, uint32_t *pos, uint32_t max, const char *s, int width, int left,
                      int zero) {
    if (!s)
        s = "(null)";
    int len = (int)strlen(s);
    int pad = (width > len) ? width - len : 0;
    if (!left) {
        char pc = zero ? '0' : ' ';
        for (int i = 0; i < pad; i++)
            _putc_buf(buf, pos, max, pc);
    }
    while (*s)
        _putc_buf(buf, pos, max, *s++);
    if (left)
        for (int i = 0; i < pad; i++)
            _putc_buf(buf, pos, max, ' ');
}
static void _puti_buf(char *buf, uint32_t *pos, uint32_t max, long val, int base, int upper,
                      int width, int left, int zero, int sign) {
    char tmp[32];
    int len = 0;
    int neg = (val < 0 && base == 10);
    unsigned long uval = neg ? (unsigned long)-val : (unsigned long)val;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (uval == 0) {
        tmp[len++] = '0';
    } else {
        while (uval) {
            tmp[len++] = digits[uval % (unsigned)base];
            uval /= (unsigned)base;
        }
    }
    if (neg)
        tmp[len++] = '-';
    else if (sign)
        tmp[len++] = '+';
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
    }
    tmp[len] = '\0';
    _puts_buf(buf, pos, max, tmp, width, left, zero);
}
int vsnprintf(char *buf, uint32_t n, const char *fmt, va_list ap) {
    if (!buf || n == 0)
        return 0;
    uint32_t pos = 0;
    while (*fmt) {
        if (*fmt != '%') {
            _putc_buf(buf, &pos, n, *fmt++);
            continue;
        }
        fmt++;
        int left = 0, zero = 0, sign = 0;
        while (*fmt == '-' || *fmt == '0' || *fmt == '+') {
            if (*fmt == '-')
                left = 1;
            if (*fmt == '0')
                zero = 1;
            if (*fmt == '+')
                sign = 1;
            fmt++;
        }
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++) - '0';
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++) - '0';
        }
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        } else if (*fmt == 'h')
            fmt++;
        char spec = *fmt++;
        switch (spec) {
        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            _puti_buf(buf, &pos, n, v, 10, 0, width, left, zero, sign);
            break;
        }
        case 'u': {
            unsigned long v =
                is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _puti_buf(buf, &pos, n, (long)v, 10, 0, width, left, zero, 0);
            break;
        }
        case 'x': {
            unsigned long v =
                is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _puti_buf(buf, &pos, n, (long)v, 16, 0, width, left, zero, 0);
            break;
        }
        case 'X': {
            unsigned long v =
                is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _puti_buf(buf, &pos, n, (long)v, 16, 1, width, left, zero, 0);
            break;
        }
        case 'o': {
            unsigned long v =
                is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _puti_buf(buf, &pos, n, (long)v, 8, 0, width, left, zero, 0);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)(uintptr_t)va_arg(ap, void *);
            _putc_buf(buf, &pos, n, '0');
            _putc_buf(buf, &pos, n, 'x');
            _puti_buf(buf, &pos, n, (long)v, 16, 0, 0, 0, 1, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            if (prec >= 0) {
                char tmp[512];
                int lim = prec < 511 ? prec : 511, i = 0;
                while (i < lim && s[i]) {
                    tmp[i] = s[i];
                    i++;
                }
                tmp[i] = '\0';
                _puts_buf(buf, &pos, n, tmp, width, left, 0);
            } else {
                _puts_buf(buf, &pos, n, s, width, left, 0);
            }
            break;
        }
        case 'c':
            _putc_buf(buf, &pos, n, (char)va_arg(ap, int));
            break;
        case '%':
            _putc_buf(buf, &pos, n, '%');
            break;
        default:
            _putc_buf(buf, &pos, n, '%');
            _putc_buf(buf, &pos, n, spec);
            break;
        }
        (void)prec;
    }
    buf[pos] = '\0';
    return (int)pos;
}
int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 4096, fmt, ap);
}
int snprintf(char *buf, uint32_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, 4096, fmt, ap);
    va_end(ap);
    return r;
}
int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    qk_print_at(0, 0, 0xFFFFFFFF, buf);
    return (int)strlen(buf);
}
int fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    qk_print_at(0, 0, 0xFFFF4444, buf);
    return (int)strlen(buf);
}
int fputs(const char *s, FILE *stream) {
    (void)stream;
    qk_print_at(0, 0, 0xFFFFFFFF, s);
    return 0;
}
int fputc(int c, FILE *stream) {
    (void)stream;
    (void)c;
    return c;
}
int puts(const char *s) {
    qk_print_at(0, 0, 0xFFFFFFFF, s);
    return 0;
}
int putchar(int c) {
    (void)c;
    return c;
}
#define CHUNK 120
static FILE _open_file;
static int _file_in_use = 0;
FILE *fopen(const char *path, const char *mode) {
    (void)mode;
    if (_file_in_use)
        return (FILE *)0;
    int spid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (spid < 0)
        return (FILE *)0;
    message_t req, rep;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = 0;
    strncpy((char *)req.payload, path, sizeof(req.payload) - 1);
    if (qk_ipc_send_recv(spid, &req, &rep) != 0)
        return (FILE *)0;
    if (rep.type != MSG_FILE_SIZE)
        return (FILE *)0;
    _open_file.size = rep.data1;
    _open_file.pos = 0;
    _open_file.storage_pid = spid;
    _open_file.is_open = 1;
    _open_file.is_stub = 0;
    _file_in_use = 1;
    return &_open_file;
}
int fclose(FILE *fp) {
    if (!fp || fp->is_stub)
        return 0;
    if (!fp->is_open)
        return -1;
    fp->is_open = 0;
    _file_in_use = 0;
    return 0;
}
uint32_t fread(void *buf, uint32_t elem_size, uint32_t count, FILE *fp) {
    if (!fp || !fp->is_open || fp->is_stub)
        return 0;
    uint32_t total = elem_size * count, read = 0;
    uint8_t *out = (uint8_t *)buf;
    while (read < total && fp->pos < fp->size) {
        uint32_t want = total - read;
        if (want > CHUNK)
            want = CHUNK;
        uint32_t avail = fp->size - fp->pos;
        if (want > avail)
            want = avail;
        message_t req, rep;
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = fp->pos;
        req.data2 = want;
        if (qk_ipc_send_recv(fp->storage_pid, &req, &rep) != 0)
            break;
        if (rep.type != MSG_FILE_DATA || rep.data1 == 0)
            break;
        uint32_t got = rep.data1;
        memcpy(out + read, rep.payload, got);
        read += got;
        fp->pos += got;
    }
    return (elem_size > 0) ? (read / elem_size) : 0;
}
uint32_t fwrite(const void *buf, uint32_t size, uint32_t count, FILE *fp) {
    (void)buf;
    (void)size;
    (void)fp;
    return count;
}
int fseek(FILE *fp, long offset, int whence) {
    if (!fp || fp->is_stub)
        return 0;
    if (!fp->is_open)
        return -1;
    long np;
    switch (whence) {
    case SEEK_SET:
        np = offset;
        break;
    case SEEK_CUR:
        np = (long)fp->pos + offset;
        break;
    case SEEK_END:
        np = (long)fp->size + offset;
        break;
    default:
        return -1;
    }
    if (np < 0)
        np = 0;
    if ((uint32_t)np > fp->size)
        np = (long)fp->size;
    fp->pos = (uint32_t)np;
    return 0;
}
long ftell(FILE *fp) {
    return (!fp || !fp->is_open) ? -1 : (long)fp->pos;
}
void rewind(FILE *fp) {
    if (fp && fp->is_open)
        fp->pos = 0;
}
int feof(FILE *fp) {
    return (!fp || !fp->is_open) ? 1 : (fp->pos >= fp->size);
}
int fgetc(FILE *fp) {
    uint8_t c;
    return (fread(&c, 1, 1, fp) == 1) ? (int)c : EOF;
}
char *fgets(char *buf, int n, FILE *fp) {
    if (!buf || n <= 0)
        return (char *)0;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(fp);
        if (c == EOF)
            break;
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (!i)
        return (char *)0;
    buf[i] = '\0';
    return buf;
}
