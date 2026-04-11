#include "lib/stdint.h"

#include "user/lib/libapi.h"

static void    *_last_alloc_ptr  = 0;
static uint32_t _last_alloc_size = 0;

void free(void *ptr)
{
    (void)ptr;
}

void *malloc(uint32_t size)
{
    if (size == 0)
        return 0;
    uint32_t aligned = (size + 3) & ~3;
    int res = qk_sbrk(aligned);
    if (res == -1)
        return 0;
    _last_alloc_ptr  = (void *)res;
    _last_alloc_size = aligned;
    return (void *)res;
}

void *realloc(void *ptr, uint32_t new_size)
{
    if (!ptr)
        return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return 0;
    }
    uint32_t aligned = (new_size + 3) & ~3;
    
    if (ptr == _last_alloc_ptr && aligned >= _last_alloc_size) {
        uint32_t extra = aligned - _last_alloc_size;
        if (extra > 0) {
            if (qk_sbrk(extra) == -1)
                return 0;
            _last_alloc_size = aligned;
        }
        return ptr;
    }
    
    if (aligned <= _last_alloc_size && ptr == _last_alloc_ptr)
        return ptr;
    
    void *np = malloc(new_size);
    if (!np)
        return 0;
    memcpy(np, ptr, _last_alloc_size < aligned ? _last_alloc_size : aligned);
    return np;
}

void memcpy32(void *dest, const void *src, uint32_t count_bytes)
{
    uint32_t num_dwords = count_bytes / 4;
    asm volatile("cld; rep movsl" : "+D"(dest), "+S"(src), "+c"(num_dwords) : : "memory");
}

void memset32(void *dest, uint32_t val, uint32_t count_dwords)
{
    asm volatile("cld; rep stosl" : "+D"(dest), "+c"(count_dwords) : "a"(val) : "memory");
}

int memcmp(const void *s1, const void *s2, uint32_t n)
{
    const uint8_t *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memcpy(void *dest, const void *src, uint32_t n)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, uint32_t n)
{
    uint8_t *p = s;
    while (n--)
        *p++ = (uint8_t)c;
    return s;
}

void *memmove(void *dest, const void *src, uint32_t n)
{
    uint8_t       *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
