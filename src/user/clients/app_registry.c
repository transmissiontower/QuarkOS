#include "app_registry.h"
#include "stdlib.h"
#include "string.h"
#include "user/clients/storage_client.h"
#include "user/libc/libapi.h"

static int folded_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

int app_registry_resolve(int storage_pid, const char *kind, const char *key, char *path,
                         uint32_t path_capacity) {
    if (!kind || !key || !path || path_capacity < 6)
        return -1;
    uint32_t size = 0;
    char *registry = storage_read_file(storage_pid, "/SYS/DAT/OPENWITH.CFG", &size);
    if (!registry)
        return -1;
    registry[size] = 0;
    int found = -1;
    char *line = registry;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        if (*end) *end++ = 0;
        char *first = line;
        char *second = first;
        while (*second && *second != '|') second++;
        if (*second) *second++ = 0;
        char *third = second;
        while (*third && *third != '|') third++;
        if (*third) *third++ = 0;
        char *binary = third;
        while (*binary && *binary != '|') binary++;
        *binary = 0;
        if (folded_equal(first, kind) && folded_equal(second, key) && *third) {
            static const char prefix[] = "/PRG/";
            uint32_t prefix_len = sizeof(prefix) - 1;
            uint32_t name_len = (uint32_t)strlen(third);
            if (prefix_len + name_len + 1 <= path_capacity) {
                memcpy(path, prefix, prefix_len);
                memcpy(path + prefix_len, third, name_len + 1);
                found = 0;
            }
            break;
        }
        line = end;
    }
    free(registry);
    return found;
}
