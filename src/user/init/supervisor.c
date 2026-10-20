#include "user/libc/libapi.h"
#include "user/init/init.h"

#define SERVICE_COUNT 5
#define PROCESS_LIMIT 64
#define SUPERVISOR_POLL_MS 250

typedef struct {
    const char *name;
    qk_server_type_t server;
    uint32_t dependencies;
    const uint8_t *image;
    uint32_t image_size;
    int pid;
    uint32_t started_at;
    uint32_t restart_at;
    uint8_t failures;
} service_t;

static service_t services[SERVICE_COUNT] = {
    {"storage_server", QK_SERVER_STORAGE, 0, 0, 0, -1, 0, 0, 0},
    {"time_server", (qk_server_type_t)-1, 0, 0, 0, -1, 0, 0, 0},
    {"ttysh", QK_SERVER_TTYSH, 1U << 0, 0, 0, -1, 0, 0, 0},
    {"comp", QK_SERVER_COMPOSITOR, 1U << 0, 0, 0, -1, 0, 0, 0},
    {"input_server", QK_SERVER_INPUT, 1U << 3, 0, 0, -1, 0, 0, 0},
};

static process_info_t process_snapshot[PROCESS_LIMIT];
static const uint8_t *initrd_data;
static uint32_t initrd_size;

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int hex_value(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int cpio_hex32(const uint8_t *field, uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < 8; i++) {
        int digit = hex_value(field[i]);
        if (digit < 0)
            return -1;
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return 0;
}

static const uint8_t *find_program(const char *name, uint32_t *image_size) {
    uint32_t cursor = 0;
    while (cursor <= initrd_size && initrd_size - cursor >= SODIUM_CPIO_NEWC_HEADER_SIZE) {
        const uint8_t *header = initrd_data + cursor;
        if (memcmp(header, "070701", 6) != 0)
            return 0;
        uint32_t file_size, name_size;
        if (cpio_hex32(header + 54, &file_size) != 0 ||
            cpio_hex32(header + 94, &name_size) != 0 || name_size == 0)
            return 0;
        uint64_t name_end = (uint64_t)cursor + 110U + name_size;
        if (name_end > initrd_size || initrd_data[name_end - 1] != 0)
            return 0;
        uint32_t data_offset = (uint32_t)((name_end + 3U) & ~3ULL);
        uint64_t data_end = (uint64_t)data_offset + file_size;
        if (data_end > initrd_size)
            return 0;

        const char *entry_name = (const char *)(initrd_data + cursor + 110U);
        if (same_name(entry_name, "TRAILER!!!"))
            return 0;
        const char *base = entry_name;
        for (const char *p = entry_name; *p; p++)
            if (*p == '/')
                base = p + 1;
        uint32_t base_len = 0;
        while (base[base_len])
            base_len++;
        uint32_t wanted_len = 0;
        while (name[wanted_len])
            wanted_len++;
        int program_name_matches = base_len == wanted_len + 4;
        for (uint32_t i = 0; program_name_matches && i < wanted_len; i++)
            if (base[i] != name[i])
                program_name_matches = 0;
        if (program_name_matches && same_name(base + wanted_len, ".qex") && file_size >= 20 &&
            initrd_data[data_offset] == 0x7f && initrd_data[data_offset + 1] == 'Q' &&
            initrd_data[data_offset + 2] == 'E' && initrd_data[data_offset + 3] == 'X') {
            *image_size = file_size;
            return initrd_data + data_offset;
        }
        cursor = (uint32_t)((data_end + 3U) & ~3ULL);
    }
    return 0;
}

static int process_is_alive(int pid, int process_count) {
    for (int i = 0; i < process_count; i++)
        if (process_snapshot[i].pid == pid)
            return 1;
    return 0;
}

static int dependencies_are_running(const service_t *service) {
    for (int i = 0; i < SERVICE_COUNT; i++)
        if ((service->dependencies & (1U << i)) && services[i].pid < 0)
            return 0;
    return 1;
}

static uint32_t restart_delay(uint8_t failures) {
    uint8_t shift = failures > 5 ? 5 : failures;
    return 1000U << shift;
}

static void start_service(service_t *service, uint32_t now) {
    if (!service->image || !dependencies_are_running(service))
        return;
    if ((int32_t)(now - service->restart_at) < 0)
        return;

    int pid = qk_spawn((void *)(uintptr_t)service->image, service->image_size);
    if (pid < 0) {
        if (service->failures < 255)
            service->failures++;
        service->restart_at = now + restart_delay(service->failures);
        return;
    }

    service->pid = pid;
    service->started_at = now;
    service->restart_at = 0;
    qk_set_proc_name(pid, service->name);
    if ((int)service->server >= 0)
        qk_assign_server(service->server, pid);
}

static void supervise_services(void) {
    for (;;) {
        uint32_t now = qk_get_uptime();
        int process_count = qk_get_procs(process_snapshot, PROCESS_LIMIT);
        if (process_count < 0)
            process_count = 0;
        if (process_count > PROCESS_LIMIT)
            process_count = PROCESS_LIMIT;

        for (int i = 0; i < SERVICE_COUNT; i++) {
            service_t *service = &services[i];
            if (service->pid >= 0 && !process_is_alive(service->pid, process_count)) {
                if ((uint32_t)(now - service->started_at) >= 30000U)
                    service->failures = 0;
                if (service->failures < 255)
                    service->failures++;
                service->restart_at = now + restart_delay(service->failures);
                service->pid = -1;
            }
            if (service->pid < 0)
                start_service(service, now);
        }
        qk_sleep(SUPERVISOR_POLL_MS);
    }
}

void main(void) {
    const sodium_init_context_t *context =
        (const sodium_init_context_t *)(uintptr_t)SODIUM_INIT_CONTEXT_ADDRESS;
    initrd_data = (const uint8_t *)(uintptr_t)context->address;
    initrd_size = context->size;
    if (initrd_size < SODIUM_CPIO_NEWC_HEADER_SIZE)
        qk_sys_exit(1);

    for (int i = 0; i < SERVICE_COUNT; i++) {
        services[i].image = find_program(services[i].name, &services[i].image_size);
        if (!services[i].image)
            qk_sys_exit(1);
    }
    supervise_services();
}
