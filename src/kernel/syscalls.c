#include "kernel/syscalls.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/acpi.h"
#include "drivers/pcspk.h"
#include "drivers/rtc.h"
#include "drivers/sb16.h"
#include "drivers/timer.h"
#include "kernel/ipc.h"
#include "kernel/process.h"
#include "lib/stdlib.h"
#include "mm/memory.h"
#include "util/util.h"
extern uint32_t total_alloc;
extern uint32_t total_phys_pages;
extern uint32_t k_font_phys_addr;
extern uint32_t k_font_size;
extern uint32_t k_icons_phys_start;
extern uint32_t k_icons_size;
extern uint32_t k_wallpaper_phys_addr;
extern uint32_t k_wallpaper_size;
int sys_map_wallpaper(uint32_t *virt_addr_out)
{
    if (k_wallpaper_phys_addr == 0)
        return -1;
    uint32_t virt_start = 0x93000000;
    uint32_t pages = (k_wallpaper_size + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        mm_vmm_map_page(virt_start + i * 4096, k_wallpaper_phys_addr + i * 4096,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER);
    }
    *virt_addr_out = virt_start;
    return 0;
}
static int storage_server_pid = -1;
static int compositor_server_pid = -1;
static int input_server_pid = -1;
static int is_ptr_safe(const void *ptr, uint32_t size)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x1000)
        return 0;
    if (addr >= KERNEL_START || (addr + size) > KERNEL_START || (addr + size) < addr)
        return 0;
    return 1;
}
int sys_grant_io_privilege(int pid, uint32_t mask)
{
    process_t *p = k_proc_find_by_id(pid);
    if (p) {
        p->io_privilege_mask |= mask;
        return 0;
    }
    return -1;
}
int sys_set_server_pid(int type, int pid)
{
    if (type == 0)
        storage_server_pid = pid;
    else if (type == 1)
        compositor_server_pid = pid;
    else if (type == 2)
        input_server_pid = pid;
    return 0;
}
int sys_get_server_pid(int type)
{
    if (type == 0)
        return storage_server_pid;
    if (type == 1)
        return compositor_server_pid;
    if (type == 2)
        return input_server_pid;
    return -1;
}
void k_notify_server_proc_exit(int pid)
{
    if (compositor_server_pid < 0)
        return;
    process_t *comp = k_proc_find_by_id(compositor_server_pid);
    if (!comp)
        return;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_PROCESS_TERMINATED;
    msg.data1 = pid;
    msg.sender_pid = 0;
    if (comp->ipc_queue.count < IPC_QUEUE_SIZE) {
        message_t *slot = &comp->ipc_queue.messages[comp->ipc_queue.tail];
        memcpy(slot, &msg, sizeof(message_t));
        comp->ipc_queue.tail = (comp->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
        comp->ipc_queue.count++;
        if (comp->state == PROCESS_BLOCKED || comp->state == PROCESS_IPC_WAIT)
            comp->state = PROCESS_READY;
    }
}
int sys_exit(int code)
{
    current_process->exit_code = code;
    k_proc_terminate(current_process->pid);
    k_scheduler_yield();
    return 0;
}
int sys_kill(int pid)
{
    if (pid <= 1)
        return -1;
    k_proc_terminate(pid);
    return 0;
}
int sys_uptime()
{
    return (int)get_uptime_ms();
}
int sys_sleep(uint32_t ms)
{
    block_current_process(ms);
    return 0;
}
int sys_spawn(void *buffer, uint32_t size)
{
    if (!buffer || size == 0)
        return -1;
    if (!is_ptr_safe(buffer, size))
        return -1;
    process_t *proc = k_proc_spawn((uint8_t *)buffer, size);
    if (!proc)
        return -1;
    return proc->pid;
}
int sys_map_fb(fb_info_t *user_info)
{
    if (!is_ptr_safe(user_info, sizeof(fb_info_t)))
        return -1;
    uint32_t virt_start = USER_FB_VIRT_START;
    uint32_t phys_base = fb_phy_addr & ~0xFFF;
    uint32_t offset = fb_phy_addr & 0xFFF;
    uint32_t size = (fb_size_bytes + offset + 0xFFF) & ~0xFFF;
    for (uint32_t i = 0; i < size; i += 4096)
        mm_vmm_map_page(virt_start + i, phys_base + i,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
    user_info->width = fb_width;
    user_info->height = fb_height;
    user_info->pitch = fb_pitch;
    user_info->bpp = fb_bpp;
    user_info->buffer = (void *)(virt_start + offset);
    return 0;
}
static void ipc_enqueue(process_t *dest, message_t *msg)
{
    message_t *slot = &dest->ipc_queue.messages[dest->ipc_queue.tail];
    memcpy(slot, msg, sizeof(message_t));
    slot->sender_pid = current_process->pid;
    dest->ipc_queue.tail = (dest->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
    dest->ipc_queue.count++;
}
static message_t *ipc_dequeue(process_t *proc)
{
    message_t *slot = &proc->ipc_queue.messages[proc->ipc_queue.head];
    proc->ipc_queue.head = (proc->ipc_queue.head + 1) % IPC_QUEUE_SIZE;
    proc->ipc_queue.count--;
    return slot;
}
int sys_ipc_send(int dest_pid, message_t *user_msg)
{
    if (!is_ptr_safe(user_msg, sizeof(message_t)))
        return -1;
    process_t *dest = k_proc_find_by_id(dest_pid);
    if (!dest)
        return -1;
    if (dest->ipc_wait_from == current_process->pid) {
        memcpy(&dest->ipc_reply_slot, user_msg, sizeof(message_t));
        dest->ipc_reply_slot.sender_pid = current_process->pid;
        dest->ipc_reply_ready = 1;
        dest->state = PROCESS_READY;
        k_scheduler_direct_switch(dest);
        return 0;
    }
    if (dest->ipc_wait_from != -1)
        return 0;
    if (dest->ipc_queue.count >= IPC_QUEUE_SIZE)
        return -1;
    ipc_enqueue(dest, user_msg);
    if (dest->state == PROCESS_IPC_WAIT || dest->state == PROCESS_BLOCKED)
        dest->state = PROCESS_READY;
    return 0;
}
int sys_ipc_send_recv(int dest_pid, message_t *send_msg, message_t *recv_msg)
{
    if (!is_ptr_safe(send_msg, sizeof(message_t)))
        return -1;
    if (!is_ptr_safe(recv_msg, sizeof(message_t)))
        return -1;
    process_t *dest = k_proc_find_by_id(dest_pid);
    if (!dest)
        return -1;
    while (dest->ipc_queue.count >= IPC_QUEUE_SIZE)
        k_scheduler_yield();
    ipc_enqueue(dest, send_msg);
    current_process->ipc_wait_from = dest_pid;
    current_process->ipc_reply_ready = 0;
    current_process->state = PROCESS_IPC_WAIT;
    if (dest->state == PROCESS_IPC_WAIT || dest->state == PROCESS_READY) {
        if (dest->state != PROCESS_RUNNING)
            k_scheduler_direct_switch(dest);
    } else {
        if (dest->state == PROCESS_BLOCKED)
            dest->state = PROCESS_READY;
        k_scheduler_yield();
    }
    while (!current_process->ipc_reply_ready) {
        current_process->state = PROCESS_IPC_WAIT;
        k_scheduler_yield();
    }
    current_process->ipc_wait_from = -1;
    memcpy(recv_msg, &current_process->ipc_reply_slot, sizeof(message_t));
    return 0;
}
int sys_ipc_recv(message_t *user_msg)
{
    if (!is_ptr_safe(user_msg, sizeof(message_t)))
        return -1;
    while (current_process->ipc_queue.count == 0) {
        current_process->state = PROCESS_IPC_WAIT;
        k_scheduler_yield();
    }
    message_t *slot = ipc_dequeue(current_process);
    memcpy(user_msg, slot, sizeof(message_t));
    return 0;
}
int sys_ipc_recv_nb(message_t *user_msg)
{
    if (!is_ptr_safe(user_msg, sizeof(message_t)))
        return -1;
    if (current_process->ipc_queue.count == 0)
        return -1;
    message_t *slot = &current_process->ipc_queue.messages[current_process->ipc_queue.head];
    memcpy(user_msg, slot, sizeof(message_t));
    current_process->ipc_queue.head = (current_process->ipc_queue.head + 1) % IPC_QUEUE_SIZE;
    current_process->ipc_queue.count--;
    return 0;
}
int sys_map_font_atlas(uint32_t *virt_addr_out)
{
    if (!is_ptr_safe(virt_addr_out, 4))
        return -1;
    if (k_font_phys_addr == 0 || k_font_size == 0)
        return -1;
    uint32_t virt_start = 0x90000000;
    uint32_t font_pages = (k_font_size + 4095) / 4096;
    for (uint32_t i = 0; i < font_pages; i++) {
        mm_vmm_map_page(virt_start + i * 4096, k_font_phys_addr + i * 4096,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER);
    }
    *virt_addr_out = virt_start;
    return 0;
}
int sys_port_io(int port, int val, int type)
{
    if (!(current_process->io_privilege_mask & 0x03))
        return -1;
    switch (type) {
    case 0:
        return (int)inb(port);
    case 1:
        outb(port, (uint8_t)val);
        break;
    case 2:
        return (int)inw(port);
    case 3:
        outw(port, (uint16_t)val);
        break;
    case 4:
        return (int)inl((uint16_t)port);
    case 5:
        outl((uint16_t)port, (uint32_t)val);
        break;
    }
    return 0;
}
int sys_map_icons_atlas(uint32_t *virt_addr_out)
{
    if (k_icons_phys_start == 0)
        return -1;
    uint32_t virt_start = 0x91000000;
    uint32_t pages = (k_icons_size + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        mm_vmm_map_page(virt_start + i * 4096, k_icons_phys_start + i * 4096,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER);
    }
    *virt_addr_out = virt_start;
    return 0;
}
int sys_sbrk(uint32_t size)
{
    uint32_t old_heap_end = current_process->heap_end;
    if (size == 0)
        return (int)old_heap_end;
    if (old_heap_end + size > 0xA0000000 || old_heap_end + size < old_heap_end) {
        return -1;
    }
    uint32_t new_heap_end = old_heap_end + size;
    uint32_t start_addr = (old_heap_end + 4095) & ~4095;
    uint32_t end_addr = (new_heap_end + 4095) & ~4095;
    for (uint32_t addr = start_addr; addr < end_addr; addr += 4096) {
        uint32_t phys = mm_pmm_alloc_frame();
        if (!phys)
            return -1;
        mm_vmm_map_page(addr, phys, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
    }
    current_process->heap_end = new_heap_end;
    return (int)old_heap_end;
}
int sys_get_vmap(vmap_region_t *out_regions)
{
    process_t *p = current_process;
    int i = 0;
    out_regions[i].start = 0x400000;
    out_regions[i].end = p->heap_end;
    out_regions[i].flags = 0x07;
    strncpy(out_regions[i].name, "Code/Data/Heap", 15);
    i++;
    out_regions[i].start = 0xAFF00000;
    out_regions[i].end = 0xB0000000;
    out_regions[i].flags = 0x07;
    strncpy(out_regions[i].name, "User Stack", 15);
    i++;
    out_regions[i].start = 0x80000000;
    out_regions[i].end = 0x80000000 + (fb_width * fb_height * 4);
    out_regions[i].flags = 0x07;
    strncpy(out_regions[i].name, "Framebuffer", 15);
    i++;
    out_regions[i].start = 0xC0000000;
    out_regions[i].end = 0xFFFFFFFF;
    out_regions[i].flags = 0x03;
    strncpy(out_regions[i].name, "Kernel Space", 15);
    i++;
    return i;
}
int sys_get_procs(process_info_t *out, int max)
{
    if (!is_ptr_safe(out, sizeof(process_info_t) * max))
        return -1;
    extern process_t *ready_queue;
    process_t *curr = ready_queue;
    int count = 0;
    do {
        out[count].pid = curr->pid;
        out[count].state = curr->state;
        strncpy(out[count].name, curr->name, 15);
        out[count].name[15] = 0;
        count++;
        curr = curr->next;
        if (count >= max)
            break;
    } while (curr != ready_queue);
    return count;
}
int sys_set_proc_name(int pid, const char *name)
{
    if (!is_ptr_safe(name, 1))
        return -1;
    process_t *p = (pid == -1) ? current_process : k_proc_find_by_id(pid);
    if (!p)
        return -1;
    strncpy(p->name, name, 15);
    p->name[15] = 0;
    return 0;
}
int sys_get_time(Time *user_time)
{
    extern volatile Time sys_time;
    user_time->second = sys_time.second;
    user_time->minute = sys_time.minute;
    user_time->hour = sys_time.hour;
    user_time->day = sys_time.day;
    user_time->month = sys_time.month;
    user_time->year = sys_time.year;
    return 0;
}
int sys_power_action(int action)
{
    if (action == 0) {
        acpi_shutdown();
    } else if (action == 1) {
        acpi_reboot();
    }
    return -1;
}
int sys_get_stats(uint32_t *used, uint32_t *total, uint32_t *cpu)
{
    if (!is_ptr_safe(used, 4) || !is_ptr_safe(total, 4) || !is_ptr_safe(cpu, 4))
        return -1;
    extern uint32_t total_alloc;
    extern uint32_t total_phys_pages;
    extern uint32_t cpu_usage_pct;
    *used = total_alloc * 4096;
    *total = total_phys_pages * 4096;
    *cpu = cpu_usage_pct;
    return 0;
}
int sys_virt_to_phys_list(uint32_t virt, uint32_t size, uint32_t *out)
{
    if (size == 0 || !out)
        return -1;
    uint32_t offset    = virt & 0xFFF;
    uint32_t page_base = virt & ~0xFFF;
    uint32_t pages     = (offset + size + 4095) / 4096;
    if (!is_ptr_safe(out, pages * 4))
        return -1;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t addr   = page_base + i * 4096;
        uint32_t pd_idx = addr >> 22;
        uint32_t pt_idx = (addr >> 12) & 0x3FF;
        uint32_t *pd    = REC_PAGEDIR;
        if (!(pd[pd_idx] & PAGE_FLAG_PRESENT))
            return -1;
        uint32_t *pt = Rec_PAGEDIR(pd_idx);
        if (!(pt[pt_idx] & PAGE_FLAG_PRESENT))
            return -1;
        out[i] = pt[pt_idx] & 0xFFFFF000;
    }
    return (int)pages;
}
uint32_t sys_map_phys_list(uint32_t *phys_pages, uint32_t count)
{
    if (!phys_pages || count == 0 || count > 4096)
        return 0;
    if (!is_ptr_safe(phys_pages, count * 4))
        return 0;
    uint32_t virt_start = current_process->shared_virt_bump;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t phys = phys_pages[i];
        if (!phys)
            return 0;
        mm_vmm_map_page(virt_start + i * 4096, phys,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
    }
    current_process->shared_virt_bump += count * 4096;
    return virt_start;
}
int sys_trigger_panic(const char *msg)
{
    if (current_process->pid != 0)
        return -1;
    const char *default_msg = "User requested panic";
    const char *panic_msg = default_msg;
    if (msg && is_ptr_safe(msg, 64)) {
        static char buffer[64];
        strncpy(buffer, msg, 63);
        buffer[63] = 0;
        panic_msg = buffer;
    }
    kernel_panic(panic_msg);
    return -1;
}
int sys_waitpid(int pid)
{
    process_t *target = k_proc_find_by_id(pid);
    if (!target)
        return -1;
    current_process->waitpid_target = pid;
    current_process->state = PROCESS_WAIT_PID;
    k_scheduler_yield();
    while (current_process->waitpid_target != -1) {
        current_process->state = PROCESS_WAIT_PID;
        k_scheduler_yield();
    }
    return (int)current_process->ipc_reply_slot.data1;
}
#define MAX_PIPES    16
#define PIPE_BUF_SIZE 4096
typedef struct {
    int      id;
    char     name[16];
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t head, tail, count;
    int      blocked_reader;
    int      blocked_writer;
} kpipe_t;
static kpipe_t k_pipes[MAX_PIPES];
static int     k_pipes_inited = 0;
static void pipes_init(void)
{
    if (k_pipes_inited) return;
    for (int i = 0; i < MAX_PIPES; i++) k_pipes[i].id = 0;
    k_pipes_inited = 1;
}
static int pipe_name_eq(const char *a, const char *b)
{
    int i = 0;
    while (i < 15 && a[i] && b[i] && a[i] == b[i]) i++;
    return (a[i] == 0 && b[i] == 0);
}
int sys_pipe_create(const char *name)
{
    if (!is_ptr_safe(name, 1)) return -1;
    pipes_init();
    for (int i = 0; i < MAX_PIPES; i++) {
        if (k_pipes[i].id == 0) {
            k_pipes[i].id = i + 1;
            strncpy(k_pipes[i].name, name, 15);
            k_pipes[i].name[15] = 0;
            k_pipes[i].head = 0;
            k_pipes[i].tail = 0;
            k_pipes[i].count = 0;
            k_pipes[i].blocked_reader = -1;
            k_pipes[i].blocked_writer = -1;
            return i + 1;
        }
    }
    return -1;
}
int sys_pipe_open(const char *name)
{
    if (!is_ptr_safe(name, 1)) return -1;
    pipes_init();
    for (int i = 0; i < MAX_PIPES; i++) {
        if (k_pipes[i].id != 0 && pipe_name_eq(k_pipes[i].name, name))
            return i + 1;
    }
    return -1;
}
int sys_pipe_write(int pipe_id, const uint8_t *buf, uint32_t len)
{
    if (pipe_id < 1 || pipe_id > MAX_PIPES) return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0 || !buf || len == 0) return -1;
    if (!is_ptr_safe(buf, len)) return -1;
    for (uint32_t w = 0; w < len; w++) {
        while (p->count >= PIPE_BUF_SIZE) {
            p->blocked_writer = current_process->pid;
            current_process->state = PROCESS_BLOCKED;
            current_process->wake_up_time = 0;
            k_scheduler_yield();
        }
        p->buf[p->tail] = buf[w];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        p->count++;
        if (p->blocked_reader != -1) {
            process_t *r = k_proc_find_by_id(p->blocked_reader);
            if (r && r->state == PROCESS_BLOCKED) r->state = PROCESS_READY;
            p->blocked_reader = -1;
        }
    }
    return (int)len;
}
int sys_pipe_read(int pipe_id, uint8_t *buf, uint32_t len)
{
    if (pipe_id < 1 || pipe_id > MAX_PIPES) return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0 || !buf || len == 0) return -1;
    if (!is_ptr_safe(buf, len)) return -1;
    for (uint32_t r = 0; r < len; r++) {
        while (p->count == 0) {
            p->blocked_reader = current_process->pid;
            current_process->state = PROCESS_BLOCKED;
            current_process->wake_up_time = 0;
            k_scheduler_yield();
        }
        buf[r] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count--;
        if (p->blocked_writer != -1) {
            process_t *w = k_proc_find_by_id(p->blocked_writer);
            if (w && w->state == PROCESS_BLOCKED) w->state = PROCESS_READY;
            p->blocked_writer = -1;
        }
    }
    return (int)len;
}
int sys_pipe_close(int pipe_id)
{
    if (pipe_id < 1 || pipe_id > MAX_PIPES) return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0) return -1;
    if (p->blocked_reader != -1) {
        process_t *r = k_proc_find_by_id(p->blocked_reader);
        if (r && r->state == PROCESS_BLOCKED) r->state = PROCESS_READY;
    }
    if (p->blocked_writer != -1) {
        process_t *w = k_proc_find_by_id(p->blocked_writer);
        if (w && w->state == PROCESS_BLOCKED) w->state = PROCESS_READY;
    }
    p->id = 0;
    return 0;
}
#define MAX_ALARMS 32
typedef struct { int pid; uint64_t fire_at; } k_alarm_entry_t;
static k_alarm_entry_t k_alarms[MAX_ALARMS];
static int k_alarms_inited = 0;
static void alarms_init(void)
{
    if (k_alarms_inited) return;
    for (int i = 0; i < MAX_ALARMS; i++) k_alarms[i].pid = 0;
    k_alarms_inited = 1;
}
int sys_set_alarm(uint32_t ms)
{
    alarms_init();
    int free_slot = -1;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (k_alarms[i].pid == current_process->pid) {
            k_alarms[i].pid = 0;
            if (free_slot < 0) free_slot = i;
        } else if (k_alarms[i].pid == 0 && free_slot < 0) {
            free_slot = i;
        }
    }
    if (ms == 0) return 0; 
    if (free_slot < 0) return -1;
    k_alarms[free_slot].pid = current_process->pid;
    k_alarms[free_slot].fire_at = get_uptime_ms() + ms;
    return 0;
}
void sys_alarm_tick(uint64_t now_ms)
{
    if (!k_alarms_inited) return;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (k_alarms[i].pid > 0 && now_ms >= k_alarms[i].fire_at) {
            process_t *target = k_proc_find_by_id(k_alarms[i].pid);
            k_alarms[i].pid = 0;
            if (!target) continue;
            if (target->ipc_queue.count >= IPC_QUEUE_SIZE) continue;
            message_t amsg;
            memset(&amsg, 0, sizeof(amsg));
            amsg.type = MSG_ALARM;
            amsg.sender_pid = 0;
            message_t *slot = &target->ipc_queue.messages[target->ipc_queue.tail];
            memcpy(slot, &amsg, sizeof(message_t));
            target->ipc_queue.tail = (target->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
            target->ipc_queue.count++;
            if (target->state == PROCESS_BLOCKED || target->state == PROCESS_IPC_WAIT)
                target->state = PROCESS_READY;
        }
    }
}
int sys_setenv(const char *kv)
{
    if (!is_ptr_safe(kv, 1)) return -1;
    const char *eq = kv;
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') return -1;
    uint32_t klen = (uint32_t)(eq - kv);
    if (klen == 0 || klen > 63) return -1;
    static char tmp[256];
    uint32_t kv_len = 0;
    while (kv[kv_len]) kv_len++;
    uint32_t out = 0;
    const char *p = current_process->env;
    while (*p) {
        const char *entry = p;
        while (*p) p++;
        p++;
        uint32_t elen = (uint32_t)(p - entry - 1);
        int match = (elen > klen);
        if (match) {
            for (uint32_t i = 0; i < klen; i++)
                if (entry[i] != kv[i]) { match = 0; break; }
            if (match) match = (entry[klen] == '=');
        }
        if (!match) {
            if (out + elen + 1 >= 256) return -1;
            memcpy(tmp + out, entry, elen);
            out += elen;
            tmp[out++] = 0;
        }
    }
    if (out + kv_len + 2 > 256) return -1;
    memcpy(tmp + out, kv, kv_len);
    out += kv_len;
    tmp[out++] = 0;
    tmp[out]   = 0;
    memcpy(current_process->env, tmp, out + 1);
    return 0;
}
int sys_getenv(const char *key, char *out, uint32_t maxlen)
{
    if (!is_ptr_safe(key, 1)) return -1;
    if (!is_ptr_safe(out, maxlen)) return -1;
    uint32_t klen = 0;
    while (key[klen]) klen++;
    const char *p = current_process->env;
    while (*p) {
        const char *next = p;
        while (*next) next++;
        next++;
        int match = 1;
        for (uint32_t i = 0; i < klen; i++)
            if (p[i] != key[i]) { match = 0; break; }
        if (match && p[klen] == '=') {
            const char *val = p + klen + 1;
            strncpy(out, val, maxlen - 1);
            out[maxlen - 1] = 0;
            return 0;
        }
        p = next;
    }
    if (maxlen > 0) out[0] = 0;
    return -1;
}
int sys_signal_send(int pid, int signum)
{
    if (signum == 9) {
        return sys_kill(pid);
    }
    process_t *target = k_proc_find_by_id(pid);
    if (!target) return -1;
    if (target->ipc_queue.count >= IPC_QUEUE_SIZE) return -1;
    message_t smsg;
    memset(&smsg, 0, sizeof(smsg));
    smsg.type = MSG_SIGNAL;
    smsg.sender_pid = current_process->pid;
    smsg.data1 = (uint32_t)signum;
    message_t *slot = &target->ipc_queue.messages[target->ipc_queue.tail];
    memcpy(slot, &smsg, sizeof(message_t));
    target->ipc_queue.tail = (target->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
    target->ipc_queue.count++;
    if (target->state == PROCESS_BLOCKED || target->state == PROCESS_IPC_WAIT)
        target->state = PROCESS_READY;
    return 0;
}
void k_syscall_entry(struct InterruptRegisters *regs)
{
    switch (regs->eax) {
    case 0:
        regs->eax = sys_exit(regs->ebx);
        break;
    case 6:
        regs->eax = sys_uptime();
        break;
    case 7:
        regs->eax = sys_sleep(regs->ebx);
        break;
    case 9:
        regs->eax = sys_spawn((void *)regs->ebx, regs->ecx);
        break;
    case 11:
        regs->eax = sys_map_fb((fb_info_t *)regs->ebx);
        break;
    case 12:
        regs->eax = sys_ipc_recv_nb((message_t *)regs->ebx);
        break;
    case 13:
        regs->eax = sys_ipc_send(regs->ebx, (message_t *)regs->ecx);
        break;
    case 14:
        regs->eax = sys_ipc_recv((message_t *)regs->ebx);
        break;
    case 15:
        regs->eax = sys_ipc_send_recv(regs->ebx, (message_t *)regs->ecx, (message_t *)regs->edx);
        break;
    case 16:
        k_proc_wait_irq(regs->ebx);
        regs->eax = 0;
        break;
    case 18:
        regs->eax = sys_grant_io_privilege(regs->ebx, regs->ecx);
        break;
    case 19:
        regs->eax = sys_set_server_pid(regs->ebx, regs->ecx);
        break;
    case 20:
        regs->eax = sys_port_io(regs->ebx, 0, 0);
        break;
    case 21:
        regs->eax = sys_port_io(regs->ebx, regs->ecx, 1);
        break;
    case 22:
        regs->eax = sys_port_io(regs->ebx, 0, 2);
        break;
    case 23:
        regs->eax = sys_port_io(regs->ebx, regs->ecx, 3);
        break;
    case 24:
        regs->eax = sys_kill(regs->ebx);
        break;
    case 25:
        regs->eax = sys_map_font_atlas((uint32_t *)regs->ebx);
        break;
    case 26:
        regs->eax = sys_sbrk(regs->ebx);
        break;
    case 32:
        regs->eax = sys_map_icons_atlas((uint32_t *)regs->ebx);
        break;
    case 33:
        regs->eax = sys_map_wallpaper((uint32_t *)regs->ebx);
        break;
    case 35:
        regs->eax = sys_get_vmap((vmap_region_t *)regs->ebx);
        break;
    case 36:
        regs->eax = sys_get_time((Time *)regs->ebx);
        break;
    case 37:
        regs->eax =
            sys_get_stats((uint32_t *)regs->ebx, (uint32_t *)regs->ecx, (uint32_t *)regs->edx);
        break;
    case 40:
        regs->eax = sys_get_procs((process_info_t *)regs->ebx, regs->ecx);
        break;
    case 41:
        regs->eax = sys_set_proc_name(regs->ebx, (const char *)regs->ecx);
        break;
    case 45:
        regs->eax = sys_power_action(regs->ebx);
        break;
    case 46:
        regs->eax = sys_get_server_pid(regs->ebx);
        break;
    case 47:
        regs->eax = sys_trigger_panic((const char *)regs->ebx);
        break;
    case 48:
        k_scheduler_yield();
        regs->eax = 0;
        break;
    case 50:
        drv_pcspk_play(regs->ebx);
        regs->eax = 0;
        break;
    case 51:
        drv_pcspk_stop();
        regs->eax = 0;
        break;
    case 52:
        drv_pcspk_pwm_start((const uint8_t *)regs->ebx,
                             (uint32_t)regs->ecx,
                             (uint32_t)regs->edx);
        regs->eax = 0;
        break;
    case 53:
        regs->eax = drv_pcspk_pwm_ready();
        break;
    case 54: {
        if (!(current_process->io_privilege_mask & IO_PRIV_ATA)) {
            regs->eax = -1;
            break;
        }
        uint32_t lba   = regs->ebx;
        uint32_t count = regs->ecx;
        uint16_t *buf  = (uint16_t *)regs->edx;
        if (!buf || count == 0 || count > 255) { regs->eax = -1; break; }
        if (!is_ptr_safe(buf, count * 512)) { regs->eax = -1; break; }
        #define ATA_SR  0x1F7
        #define ATA_DH  0x1F6
        #define ATA_SC  0x1F2
        #define ATA_LB0 0x1F3
        #define ATA_LB1 0x1F4
        #define ATA_LB2 0x1F5
        #define ATA_CMD 0x1F7
        #define ATA_DAT 0x1F0
        uint8_t slave_mask_saved = inb(0xA1);
        outb(0xA1, slave_mask_saved | 0x40);
        for (int t = 0; t < 100000; t++) { if (!(inb(ATA_SR) & 0x80)) break; }
        outb(ATA_DH,  0xE0 | ((lba >> 24) & 0x0F));
        outb(0x1F1,   0x00);
        outb(ATA_SC,  (uint8_t)count);
        outb(ATA_LB0, (uint8_t)(lba & 0xFF));
        outb(ATA_LB1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LB2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_CMD, 0x20);  
        int ok = 1;
        for (uint32_t s = 0; s < count; s++) {
            for (int t = 0; t < 100000; t++) {
                uint8_t st = inb(ATA_SR);
                if (st & 0x08) break;  
                if (st & 0x01) { ok = 0; break; }
            }
            if (!ok) break;
            uint16_t *dst = buf + s * 256;
            asm volatile("rep insw" : "+D"(dst) : "c"(256), "d"(ATA_DAT) : "memory");
            inb(ATA_SR);  
        }
        outb(0xA1, slave_mask_saved);
        asm volatile("sti");
        k_scheduler_yield();
        asm volatile("cli");
        regs->eax = ok ? 0 : -1;
        break;
    }
    case 55:
        regs->eax = current_process->pid;
        break;
    case 56:
        regs->eax = (uint32_t)sys_virt_to_phys_list(regs->ebx, regs->ecx, (uint32_t *)regs->edx);
        break;
    case 57:
        regs->eax = sys_map_phys_list((uint32_t *)regs->ebx, regs->ecx);
        break;
    case 58: {
        int      src_pid  = (int)regs->ebx;
        uint32_t src_virt = (uint32_t)regs->ecx;
        uint32_t size     = (uint32_t)regs->edx;
        if (size == 0 || size > 16 * 1024 * 1024) { regs->eax = 0; break; }
        process_t *src = k_proc_find_by_id(src_pid);
        if (!src) { regs->eax = 0; break; }
        uint32_t offset     = src_virt & 0xFFFu;
        uint32_t page_base  = src_virt & ~0xFFFu;
        uint32_t page_count = (offset + size + 4095) / 4096;
        if (page_count > 4096) { regs->eax = 0; break; }
        uint32_t saved_cr3 = current_process->pd_phys;
        asm volatile("mov %0, %%cr3" ::"r"(src->pd_phys) : "memory");
        uint32_t virt_start = current_process->shared_virt_bump;
        int ok = 1;
        asm volatile("mov %0, %%cr3" ::"r"(saved_cr3) : "memory");
        uint32_t *src_pd_virt = 0;
        uint32_t temp_pd_addr = 0xFF800000; 
        mm_vmm_map_page(temp_pd_addr, src->pd_phys,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);
        src_pd_virt = (uint32_t *)temp_pd_addr;
        for (uint32_t i = 0; i < page_count && ok; i++) {
            uint32_t addr   = page_base + i * 4096;
            uint32_t pd_idx = addr >> 22;
            uint32_t pt_idx = (addr >> 12) & 0x3FF;
            uint32_t pde = src_pd_virt[pd_idx];
            if (!(pde & PAGE_FLAG_PRESENT)) { ok = 0; break; }
            uint32_t temp_pt_addr = 0xFF801000;
            mm_vmm_map_page(temp_pt_addr, pde & ~0xFFF,
                            PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);
            uint32_t *src_pt_virt = (uint32_t *)temp_pt_addr;
            uint32_t pte = src_pt_virt[pt_idx];
            if (!(pte & PAGE_FLAG_PRESENT)) { ok = 0; break; }
            uint32_t phys = pte & ~0xFFFu;
            mm_vmm_map_page(virt_start + i * 4096, phys,
                            PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
        }
        if (ok) {
            current_process->shared_virt_bump += page_count * 4096;
            regs->eax = virt_start + offset;
        } else {
            regs->eax = 0;
        }
        break;
    }
    case 59:
        regs->eax = sys_port_io(regs->ebx, 0, 4);
        break;
    case 60:
        regs->eax = sys_port_io(regs->ebx, regs->ecx, 5);
        break;
    case 61:
        regs->eax = (uint32_t)drv_sb16_init();
        break;
    case 62: {
        void    *buf  = (void *)regs->ebx;
        uint32_t len  = regs->ecx;
        uint32_t rate = regs->edx & 0xFFFF;
        uint8_t  ch   = (uint8_t)((regs->edx >> 16) & 0xFF);
        uint8_t  bits = (uint8_t)((regs->edx >> 24) & 0xFF);
        if (ch == 0) ch = 1;
        if (bits == 0) bits = 16;
        if (!is_ptr_safe(buf, len)) { regs->eax = (uint32_t)-1; break; }
        regs->eax = (uint32_t)drv_sb16_play(buf, len, rate, ch, bits);
        break;
    }
    case 63:
        regs->eax = (uint32_t)drv_sb16_ready();
        break;
    case 64:
        regs->eax = (uint32_t)sys_waitpid((int)regs->ebx);
        break;
    case 65:
        regs->eax = (uint32_t)sys_pipe_create((const char *)regs->ebx);
        break;
    case 66:
        regs->eax = (uint32_t)sys_pipe_open((const char *)regs->ebx);
        break;
    case 67:
        regs->eax = (uint32_t)sys_pipe_write((int)regs->ebx, (const uint8_t *)regs->ecx, regs->edx);
        break;
    case 68:
        regs->eax = (uint32_t)sys_pipe_read((int)regs->ebx, (uint8_t *)regs->ecx, regs->edx);
        break;
    case 69:
        regs->eax = (uint32_t)sys_pipe_close((int)regs->ebx);
        break;
    case 70:
        regs->eax = (uint32_t)sys_set_alarm(regs->ebx);
        break;
    case 71:
        regs->eax = (uint32_t)sys_setenv((const char *)regs->ebx);
        break;
    case 72:
        regs->eax = (uint32_t)sys_getenv((const char *)regs->ebx, (char *)regs->ecx, regs->edx);
        break;
    case 73:
        regs->eax = (uint32_t)sys_signal_send((int)regs->ebx, (int)regs->ecx);
        break;
    default:
        regs->eax = -1;
        break;
    }
}
