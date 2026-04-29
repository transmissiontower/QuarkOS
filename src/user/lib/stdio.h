// SPDX-License-Identifier: MIT
/*
 * src/user/lib/stdio.h - Standard I/O types and function declarations
 */
#ifndef USER_STDIO_H
#define USER_STDIO_H
#include "lib/stdint.h"
#include "lib/stdarg.h"
#define EOF     (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct {
    uint32_t size;
    uint32_t pos;
    int      storage_pid;
    int      is_open;
    int      is_stub;
} FILE;
extern FILE *stderr;
extern FILE *stdout;
extern FILE *stdin;
int vsnprintf(char *buf, uint32_t n, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int snprintf(char *buf, uint32_t n, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
int puts(const char *s);
int putchar(int c);
FILE    *fopen(const char *path, const char *mode);
int      fclose(FILE *fp);
uint32_t fread(void *buf, uint32_t size, uint32_t count, FILE *fp);
uint32_t fwrite(const void *buf, uint32_t size, uint32_t count, FILE *fp);
int      fseek(FILE *fp, long offset, int whence);
long     ftell(FILE *fp);
void     rewind(FILE *fp);
int      feof(FILE *fp);
int      fgetc(FILE *fp);
char    *fgets(char *buf, int n, FILE *fp);
#endif
