#include "lib/stdint.h"

#include "user/lib/libapi.h"

void free(void* ptr) {
    (void)ptr;
}

void* malloc(uint32_t size) {
    if (size == 0) return 0;
    int res = qk_sbrk((size + 3) & ~3);
    if (res == -1) return 0;
    return (void*)res;
}

void memcpy32(void *dest, const void *src, uint32_t count_bytes) {
    uint32_t num_dwords = count_bytes / 4;
    asm volatile ("cld; rep movsl" : "+D"(dest), "+S"(src), "+c"(num_dwords) : : "memory");
}

void memset32(void *dest, uint32_t val, uint32_t count_dwords) {
    asm volatile ("cld; rep stosl" : "+D"(dest), "+c"(count_dwords) : "a"(val) : "memory");
}

int memcmp(const void* s1, const void* s2, uint32_t n) {
    const uint8_t *p1 = s1, *p2 = s2;
    while(n--) {
        if(*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

void* memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while(n--) *d++ = *s++;
    return dest;
}

void* memset(void* s, int c, uint32_t n) {
    uint8_t *p = s;
    while(n--) *p++ = (uint8_t)c;
    return s;
}
