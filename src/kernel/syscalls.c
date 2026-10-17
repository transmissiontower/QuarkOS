// SPDX-License-Identifier: MIT
#include "kernel/syscalls.h"
#include "kernel/klog.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "kernel/ipc.h"
#include "kernel/process.h"
#include "lib/stdlib.h"
#include "mm/memory.h"
#include "util/util.h"
#include "arch/aarch64/platform.h"
extern uint32_t total_alloc;
extern uint32_t total_phys_pages;
extern uint32_t k_font_phys_addr;
extern uint32_t k_font_size;
extern void *k_font_virt_addr;
extern uint32_t k_icons_phys_start;
extern uint32_t k_icons_size;
extern uint32_t k_wallpaper_phys_addr;
extern uint32_t k_wallpaper_size;
int sys_map_wallpaper(uint32_t *virt_addr_out) {
    if (k_wallpaper_phys_addr == 0)
        return -1;
    uint32_t virt_start = 0x93000000;
    uint32_t phys_base = k_wallpaper_phys_addr & ~0xFFFU;
    uint32_t offset = k_wallpaper_phys_addr & 0xFFFU;
    uint32_t pages = (k_wallpaper_size + offset + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        mm_vmm_map_page(virt_start + i * 4096, phys_base + i * 4096,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
    }
    *virt_addr_out = virt_start + offset;
    return 0;
}
static int storage_server_pid = -1;
static int compositor_server_pid = -1;
static int input_server_pid = -1;
static int ttysh_server_pid = -1;
static int is_ptr_safe(const void *ptr, uint32_t size) {
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x1000)
        return 0;
    if (addr >= KERNEL_START || (addr + size) > KERNEL_START || (addr + size) < addr)
        return 0;
    return 1;
}
int sys_set_server_pid(int type, int pid) {
    if (type == 0)
        storage_server_pid = pid;
    else if (type == 1)
        compositor_server_pid = pid;
    else if (type == 2)
        input_server_pid = pid;
    else if (type == 3)
        ttysh_server_pid = pid;
    return 0;
}
int sys_get_server_pid(int type) {
    if (type == 0)
        return storage_server_pid;
    if (type == 1)
        return compositor_server_pid;
    if (type == 2)
        return input_server_pid;
    if (type == 3)
        return ttysh_server_pid;
    return -1;
}
void k_notify_server_proc_exit(int pid) {
    if (compositor_server_pid < 0)
        return;
    process_t *comp = k_proc_find_by_id(compositor_server_pid);
    if (!comp)
        return;
    if (comp->ipc_queue.count < IPC_QUEUE_SIZE) {
        message_t *slot = &comp->ipc_queue.messages[comp->ipc_queue.tail];
        memset(slot, 0, sizeof(message_t));
        slot->type = MSG_PROCESS_TERMINATED;
        slot->data1 = (uint32_t)pid;
        slot->sender_pid = 0;
        comp->ipc_queue.tail = (comp->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
        comp->ipc_queue.count++;
        if (comp->state == PROCESS_BLOCKED || comp->state == PROCESS_IPC_WAIT)
            comp->state = PROCESS_READY;
    }
}
int sys_exit(int code) {
    KLOG_PROC(LOG_INFO, "EXIT pid=%d name='%s' code=%d", current_process->pid,
              current_process->name, code);
    current_process->exit_code = code;
    k_proc_terminate(current_process->pid);
    k_scheduler_yield();
    return 0;
}
int sys_kill(int pid) {
    if (pid <= 1)
        return -1;
    KLOG_PROC(LOG_WARN, "KILL pid=%d by=%d name='%s'", pid, current_process->pid,
              current_process->name);
    k_proc_terminate(pid);
    return 0;
}
int sys_uptime() {
    return (int)get_uptime_ms();
}
int sys_sleep(uint32_t ms) {
    block_current_process(ms);
    return 0;
}
int sys_spawn(void *buffer, uint32_t size) {
    if (!buffer || size == 0)
        return -1;
    if (!is_ptr_safe(buffer, size))
        return -1;
    process_t *proc = k_proc_spawn((uint8_t *)buffer, size);
    if (!proc)
        return -1;
    return proc->pid;
}
int sys_map_fb(fb_info_t *user_info) {
    if (!is_ptr_safe(user_info, sizeof(fb_info_t)))
        return -1;
    uint32_t virt_start = USER_FB_VIRT_START;
    uint32_t phys_base = fb_phy_addr & ~0xFFF;
    uint32_t offset = fb_phy_addr & 0xFFF;
    uint32_t size = (fb_size_bytes + offset + 0xFFF) & ~0xFFF;
    for (uint32_t i = 0; i < size; i += 4096)
        mm_vmm_map_page(virt_start + i, phys_base + i,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
    user_info->width = fb_width;
    user_info->height = fb_height;
    user_info->pitch = fb_pitch;
    user_info->bpp = fb_bpp;
    user_info->buffer = (void *)(uintptr_t)(virt_start + offset);
    return 0;
}
static void ipc_enqueue(process_t *dest, message_t *msg) {
    message_t *slot = &dest->ipc_queue.messages[dest->ipc_queue.tail];
    memcpy(slot, msg, sizeof(message_t));
    slot->sender_pid = current_process->pid;
    dest->ipc_queue.tail = (dest->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
    dest->ipc_queue.count++;
}
static message_t *ipc_dequeue(process_t *proc) {
    message_t *slot = &proc->ipc_queue.messages[proc->ipc_queue.head];
    proc->ipc_queue.head = (proc->ipc_queue.head + 1) % IPC_QUEUE_SIZE;
    proc->ipc_queue.count--;
    return slot;
}
int sys_ipc_send(int dest_pid, message_t *user_msg) {
    if (!is_ptr_safe(user_msg, sizeof(message_t)))
        return -1;
    process_t *dest = k_proc_find_by_id(dest_pid);
    if (!dest) {
        KLOG_IPC(LOG_WARN, "send: pid=%d→%d NOENT", current_process->pid, dest_pid);
        return -1;
    }
    if (dest->ipc_wait_from == current_process->pid) {
        KLOG_IPC(LOG_TRACE, "reply: pid=%d→%d type=%d", current_process->pid, dest_pid,
                 user_msg->type);
        memcpy(&dest->ipc_reply_slot, user_msg, sizeof(message_t));
        dest->ipc_reply_slot.sender_pid = current_process->pid;
        dest->ipc_reply_ready = 1;
        dest->state = PROCESS_READY;
        k_scheduler_direct_switch(dest);
        return 0;
    }
    if (dest->ipc_wait_from != -1)
        return 0;
    if (dest->ipc_queue.count >= IPC_QUEUE_SIZE) {
        KLOG_IPC(LOG_WARN, "send: pid=%d→%d FULL (type=%d q=%d)", current_process->pid, dest_pid,
                 user_msg->type, dest->ipc_queue.count);
        return -1;
    }
    KLOG_IPC(LOG_TRACE, "send: pid=%d→%d type=%d q=%d", current_process->pid, dest_pid,
             user_msg->type, dest->ipc_queue.count);
    ipc_enqueue(dest, user_msg);
    if (dest->state == PROCESS_IPC_WAIT || dest->state == PROCESS_BLOCKED)
        dest->state = PROCESS_READY;
    return 0;
}
int sys_ipc_send_recv(int dest_pid, message_t *send_msg, message_t *recv_msg) {
    if (!is_ptr_safe(send_msg, sizeof(message_t)))
        return -1;
    if (!is_ptr_safe(recv_msg, sizeof(message_t)))
        return -1;
    process_t *dest = k_proc_find_by_id(dest_pid);
    if (!dest) {
        KLOG_IPC(LOG_WARN, "sendrecv: pid=%d→%d NOENT", current_process->pid, dest_pid);
        return -1;
    }
    if (dest->ipc_queue.count >= IPC_QUEUE_SIZE)
        KLOG_IPC(LOG_WARN, "sendrecv: pid=%d→%d FULL q=%d, spinning", current_process->pid,
                 dest_pid, dest->ipc_queue.count);
    while (dest->ipc_queue.count >= IPC_QUEUE_SIZE)
        k_scheduler_yield();
    KLOG_IPC(LOG_TRACE, "sendrecv: pid=%d→%d type=%d (wait reply)", current_process->pid, dest_pid,
             send_msg->type);
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
    KLOG_IPC(LOG_TRACE, "sendrecv: pid=%d←%d reply type=%d", current_process->pid, dest_pid,
             current_process->ipc_reply_slot.type);
    memcpy(recv_msg, &current_process->ipc_reply_slot, sizeof(message_t));
    return 0;
}
int sys_ipc_recv(message_t *user_msg) {
    if (!is_ptr_safe(user_msg, sizeof(message_t)))
        return -1;
    while (current_process->ipc_queue.count == 0) {
        current_process->state = PROCESS_IPC_WAIT;
        k_scheduler_yield();
    }
    message_t *slot = ipc_dequeue(current_process);
    KLOG_IPC(LOG_TRACE, "recv: pid=%d←%d type=%d q=%d", current_process->pid, slot->sender_pid,
             slot->type, current_process->ipc_queue.count);
    memcpy(user_msg, slot, sizeof(message_t));
    return 0;
}
int sys_ipc_recv_nb(message_t *user_msg) {
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
int sys_map_font_atlas(uint32_t *virt_addr_out) {
    if (!is_ptr_safe(virt_addr_out, 4))
        return -1;
    uint32_t virt_start = 0x90000000;
    if (k_font_phys_addr != 0 && k_font_size != 0) {
        uint32_t phys_base = k_font_phys_addr & ~0xFFFu;
        uint32_t offset = k_font_phys_addr & 0xFFFu;
        uint32_t font_pages = (k_font_size + offset + 4095) / 4096;
        for (uint32_t i = 0; i < font_pages; i++) {
            mm_vmm_map_page(virt_start + i * 4096, phys_base + i * 4096,
                            PAGE_FLAG_PRESENT | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
        }
        *virt_addr_out = virt_start + offset;
        return 0;
    }

    if (!k_font_virt_addr || k_font_size == 0)
        return -1;
    uint32_t offset = (uint32_t)(uintptr_t)k_font_virt_addr & 0xFFFu;
    uint32_t font_pages = (offset + k_font_size + 4095) / 4096;
    uint32_t base = (uint32_t)(uintptr_t)k_font_virt_addr & ~0xFFFu;
    for (uint32_t i = 0; i < font_pages; i++) {
        uint32_t addr = base + i * 4096;
        uint32_t phys;
        phys = mm_virt_to_phys(addr) & ~0xFFFu;
        if (!phys)
            continue;
        mm_vmm_map_page(virt_start + i * 4096, phys,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
    }
    *virt_addr_out = virt_start + ((uint32_t)(uintptr_t)k_font_virt_addr & 0xFFFu);
    return 0;
}
int sys_map_icons_atlas(uint32_t *virt_addr_out) {
    if (k_icons_phys_start == 0)
        return -1;
    uint32_t virt_start = 0x91000000;
    uint32_t phys_base = k_icons_phys_start & ~0xFFFU;
    uint32_t offset = k_icons_phys_start & 0xFFFU;
    uint32_t pages = (k_icons_size + offset + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        mm_vmm_map_page(virt_start + i * 4096, phys_base + i * 4096,
                        PAGE_FLAG_PRESENT | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
    }
    *virt_addr_out = virt_start + offset;
    return 0;
}
int sys_sbrk(uint32_t size) {
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
int sys_get_vmap(vmap_region_t *out_regions) {
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
int sys_get_procs(process_info_t *out, int max) {
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
int sys_set_proc_name(int pid, const char *name) {
    if (!is_ptr_safe(name, 1))
        return -1;
    process_t *p = (pid == -1) ? current_process : k_proc_find_by_id(pid);
    if (!p)
        return -1;
    strncpy(p->name, name, 15);
    p->name[15] = 0;
    return 0;
}
int sys_get_time(Time *user_time) {
    extern volatile Time sys_time;
    user_time->second = sys_time.second;
    user_time->minute = sys_time.minute;
    user_time->hour = sys_time.hour;
    user_time->day = sys_time.day;
    user_time->month = sys_time.month;
    user_time->year = sys_time.year;
    return 0;
}
int sys_power_action(int action) {
    if (action == 0)
        arm_psci_system_off();
    if (action == 1)
        arm_psci_system_reset();
    return -1;
}
int sys_get_stats(uint32_t *used, uint32_t *total, uint32_t *cpu) {
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
int sys_virt_to_phys_list(uint32_t virt, uint32_t size, uint32_t *out) {
    if (size == 0 || !out)
        return -1;
    uint32_t offset = virt & 0xFFF;
    uint32_t page_base = virt & ~0xFFF;
    uint32_t pages = (offset + size + 4095) / 4096;
    if (!is_ptr_safe(out, pages * 4))
        return -1;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t addr = page_base + i * 4096;
        uint32_t phys = mm_virt_to_phys(addr);
        if (!phys)
            return -1;
        out[i] = phys & 0xFFFFF000;
    }
    return (int)pages;
}
uint32_t sys_map_phys_list(uint32_t *phys_pages, uint32_t count) {
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
                        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER | PAGE_FLAG_SHARED);
    }
    current_process->shared_virt_bump += count * 4096;
    return virt_start;
}
int sys_trigger_panic(const char *msg) {
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
int sys_waitpid(int pid) {
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
#define MAX_PIPES 16
#define PIPE_BUF_SIZE 4096
typedef struct {
    int id;
    char name[16];
    uint8_t buf[PIPE_BUF_SIZE];
    uint32_t head, tail, count;
    int blocked_reader;
    int blocked_writer;
} kpipe_t;
static kpipe_t k_pipes[MAX_PIPES];
static int k_pipes_inited = 0;
static void pipes_init(void) {
    if (k_pipes_inited)
        return;
    for (int i = 0; i < MAX_PIPES; i++)
        k_pipes[i].id = 0;
    k_pipes_inited = 1;
}
static int pipe_name_eq(const char *a, const char *b) {
    int i = 0;
    while (i < 15 && a[i] && b[i] && a[i] == b[i])
        i++;
    return (a[i] == 0 && b[i] == 0);
}
int sys_pipe_create(const char *name) {
    if (!is_ptr_safe(name, 1))
        return -1;
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
int sys_pipe_open(const char *name) {
    if (!is_ptr_safe(name, 1))
        return -1;
    pipes_init();
    for (int i = 0; i < MAX_PIPES; i++) {
        if (k_pipes[i].id != 0 && pipe_name_eq(k_pipes[i].name, name))
            return i + 1;
    }
    return -1;
}
int sys_pipe_write(int pipe_id, const uint8_t *buf, uint32_t len) {
    if (pipe_id < 1 || pipe_id > MAX_PIPES)
        return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0 || !buf || len == 0)
        return -1;
    if (!is_ptr_safe(buf, len))
        return -1;
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
            if (r && r->state == PROCESS_BLOCKED)
                r->state = PROCESS_READY;
            p->blocked_reader = -1;
        }
    }
    return (int)len;
}
int sys_pipe_read(int pipe_id, uint8_t *buf, uint32_t len) {
    if (pipe_id < 1 || pipe_id > MAX_PIPES)
        return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0 || !buf || len == 0)
        return -1;
    if (!is_ptr_safe(buf, len))
        return -1;
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
            if (w && w->state == PROCESS_BLOCKED)
                w->state = PROCESS_READY;
            p->blocked_writer = -1;
        }
    }
    return (int)len;
}
int sys_pipe_close(int pipe_id) {
    if (pipe_id < 1 || pipe_id > MAX_PIPES)
        return -1;
    kpipe_t *p = &k_pipes[pipe_id - 1];
    if (p->id == 0)
        return -1;
    if (p->blocked_reader != -1) {
        process_t *r = k_proc_find_by_id(p->blocked_reader);
        if (r && r->state == PROCESS_BLOCKED)
            r->state = PROCESS_READY;
    }
    if (p->blocked_writer != -1) {
        process_t *w = k_proc_find_by_id(p->blocked_writer);
        if (w && w->state == PROCESS_BLOCKED)
            w->state = PROCESS_READY;
    }
    p->id = 0;
    return 0;
}
#define MAX_ALARMS 32
typedef struct {
    int pid;
    uint64_t fire_at;
} k_alarm_entry_t;
static k_alarm_entry_t k_alarms[MAX_ALARMS];
static int k_alarms_inited = 0;
static void alarms_init(void) {
    if (k_alarms_inited)
        return;
    for (int i = 0; i < MAX_ALARMS; i++)
        k_alarms[i].pid = 0;
    k_alarms_inited = 1;
}
int sys_set_alarm(uint32_t ms) {
    alarms_init();
    int free_slot = -1;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (k_alarms[i].pid == current_process->pid) {
            k_alarms[i].pid = 0;
            if (free_slot < 0)
                free_slot = i;
        } else if (k_alarms[i].pid == 0 && free_slot < 0) {
            free_slot = i;
        }
    }
    if (ms == 0)
        return 0;
    if (free_slot < 0)
        return -1;
    k_alarms[free_slot].pid = current_process->pid;
    k_alarms[free_slot].fire_at = get_uptime_ms() + ms;
    return 0;
}
void sys_alarm_tick(uint64_t now_ms) {
    if (!k_alarms_inited)
        return;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (k_alarms[i].pid > 0 && now_ms >= k_alarms[i].fire_at) {
            process_t *target = k_proc_find_by_id(k_alarms[i].pid);
            k_alarms[i].pid = 0;
            if (!target)
                continue;
            if (target->ipc_queue.count >= IPC_QUEUE_SIZE)
                continue;
            message_t *slot = &target->ipc_queue.messages[target->ipc_queue.tail];
            memset(slot, 0, sizeof(message_t));
            slot->type = MSG_ALARM;
            slot->sender_pid = 0;
            target->ipc_queue.tail = (target->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
            target->ipc_queue.count++;
            if (target->state == PROCESS_BLOCKED || target->state == PROCESS_IPC_WAIT)
                target->state = PROCESS_READY;
        }
    }
}
int sys_setenv(const char *kv) {
    if (!is_ptr_safe(kv, 1))
        return -1;
    const char *eq = kv;
    while (*eq && *eq != '=')
        eq++;
    if (*eq != '=')
        return -1;
    uint32_t klen = (uint32_t)(eq - kv);
    if (klen == 0 || klen > 63)
        return -1;
    static char tmp[256];
    uint32_t kv_len = 0;
    while (kv[kv_len])
        kv_len++;
    uint32_t out = 0;
    const char *p = current_process->env;
    while (*p) {
        const char *entry = p;
        while (*p)
            p++;
        p++;
        uint32_t elen = (uint32_t)(p - entry - 1);
        int match = (elen > klen);
        if (match) {
            for (uint32_t i = 0; i < klen; i++)
                if (entry[i] != kv[i]) {
                    match = 0;
                    break;
                }
            if (match)
                match = (entry[klen] == '=');
        }
        if (!match) {
            if (out + elen + 1 >= 256)
                return -1;
            memcpy(tmp + out, entry, elen);
            out += elen;
            tmp[out++] = 0;
        }
    }
    if (out + kv_len + 2 > 256)
        return -1;
    memcpy(tmp + out, kv, kv_len);
    out += kv_len;
    tmp[out++] = 0;
    tmp[out] = 0;
    memcpy(current_process->env, tmp, out + 1);
    return 0;
}
int sys_getenv(const char *key, char *out, uint32_t maxlen) {
    if (!is_ptr_safe(key, 1))
        return -1;
    if (!is_ptr_safe(out, maxlen))
        return -1;
    uint32_t klen = 0;
    while (key[klen])
        klen++;
    const char *p = current_process->env;
    while (*p) {
        const char *next = p;
        while (*next)
            next++;
        next++;
        int match = 1;
        for (uint32_t i = 0; i < klen; i++)
            if (p[i] != key[i]) {
                match = 0;
                break;
            }
        if (match && p[klen] == '=') {
            const char *val = p + klen + 1;
            strncpy(out, val, maxlen - 1);
            out[maxlen - 1] = 0;
            return 0;
        }
        p = next;
    }
    if (maxlen > 0)
        out[0] = 0;
    return -1;
}
int sys_signal_send(int pid, int signum) {
    KLOG_PROC(LOG_DEBUG, "SIGNAL pid=%d→%d sig=%d", current_process->pid, pid, signum);
    if (signum == 9) {
        return sys_kill(pid);
    }
    process_t *target = k_proc_find_by_id(pid);
    if (!target)
        return -1;
    if (target->ipc_queue.count >= IPC_QUEUE_SIZE)
        return -1;
    message_t *slot = &target->ipc_queue.messages[target->ipc_queue.tail];
    memset(slot, 0, sizeof(message_t));
    slot->type = MSG_SIGNAL;
    slot->sender_pid = current_process->pid;
    slot->data1 = (uint32_t)signum;
    target->ipc_queue.tail = (target->ipc_queue.tail + 1) % IPC_QUEUE_SIZE;
    target->ipc_queue.count++;
    if (target->state == PROCESS_BLOCKED || target->state == PROCESS_IPC_WAIT)
        target->state = PROCESS_READY;
    return 0;
}
void k_syscall_entry(struct InterruptRegisters *regs) {
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
        regs->eax = sys_spawn((void *)(uintptr_t)regs->ebx, regs->ecx);
        break;
    case 11:
        regs->eax = sys_map_fb((fb_info_t *)(uintptr_t)regs->ebx);
        break;
    case 12:
        regs->eax = sys_ipc_recv_nb((message_t *)(uintptr_t)regs->ebx);
        break;
    case 13:
        regs->eax = sys_ipc_send(regs->ebx, (message_t *)(uintptr_t)regs->ecx);
        break;
    case 14:
        regs->eax = sys_ipc_recv((message_t *)(uintptr_t)regs->ebx);
        break;
    case 15:
        regs->eax = sys_ipc_send_recv(regs->ebx, (message_t *)(uintptr_t)regs->ecx,
                                      (message_t *)(uintptr_t)regs->edx);
        break;
    case 19:
        regs->eax = sys_set_server_pid(regs->ebx, regs->ecx);
        break;
    case 20:
        regs->eax = virtio_input_compat_read((uint16_t)regs->ebx);
        break;
    case 24:
        regs->eax = sys_kill(regs->ebx);
        break;
    case 25:
        regs->eax = sys_map_font_atlas((uint32_t *)(uintptr_t)regs->ebx);
        break;
    case 26:
        regs->eax = sys_sbrk(regs->ebx);
        break;
    case 32:
        regs->eax = sys_map_icons_atlas((uint32_t *)(uintptr_t)regs->ebx);
        break;
    case 33:
        regs->eax = sys_map_wallpaper((uint32_t *)(uintptr_t)regs->ebx);
        break;
    case 35:
        regs->eax = sys_get_vmap((vmap_region_t *)(uintptr_t)regs->ebx);
        break;
    case 36:
        regs->eax = sys_get_time((Time *)(uintptr_t)regs->ebx);
        break;
    case 37:
        regs->eax =
            sys_get_stats((uint32_t *)(uintptr_t)regs->ebx, (uint32_t *)(uintptr_t)regs->ecx,
                          (uint32_t *)(uintptr_t)regs->edx);
        break;
    case 40:
        regs->eax = sys_get_procs((process_info_t *)(uintptr_t)regs->ebx, regs->ecx);
        break;
    case 41:
        regs->eax = sys_set_proc_name(regs->ebx, (const char *)(uintptr_t)regs->ecx);
        break;
    case 45:
        regs->eax = sys_power_action(regs->ebx);
        break;
    case 46:
        regs->eax = sys_get_server_pid(regs->ebx);
        break;
    case 47:
        regs->eax = sys_trigger_panic((const char *)(uintptr_t)regs->ebx);
        break;
    case 48:
        k_scheduler_yield();
        regs->eax = 0;
        break;
    case 55:
        regs->eax = current_process->pid;
        break;
    case 56:
        regs->eax = (uint32_t)sys_virt_to_phys_list(regs->ebx, regs->ecx,
                                                    (uint32_t *)(uintptr_t)regs->edx);
        break;
    case 57:
        regs->eax = sys_map_phys_list((uint32_t *)(uintptr_t)regs->ebx, regs->ecx);
        break;
    case 58: {
        int src_pid = (int)regs->ebx;
        uint32_t src_virt = (uint32_t)regs->ecx;
        uint32_t size = (uint32_t)regs->edx;
        if (size == 0 || size > 16 * 1024 * 1024) {
            regs->eax = 0;
            break;
        }
        process_t *src = k_proc_find_by_id(src_pid);
        if (!src) {
            regs->eax = 0;
            break;
        }
        uint32_t offset = src_virt & 0xFFFu;
        uint32_t page_base = src_virt & ~0xFFFu;
        uint32_t page_count = (offset + size + 4095) / 4096;
        if (page_count > 4096) {
            regs->eax = 0;
            break;
        }
        uint32_t virt_start = current_process->shared_virt_bump;
        int ok = 1;
        uint64_t *src_l1 = (uint64_t *)(uintptr_t)(src->pd_phys + 0x80000000U);
        for (uint32_t i = 0; i < page_count && ok; i++) {
            uint32_t addr = page_base + i * 4096;
            uint32_t l1_idx = (addr >> 30) & 0x1FF;
            uint32_t l2_idx = (addr >> 21) & 0x1FF;
            uint32_t l3_idx = (addr >> 12) & 0x1FF;
            uint64_t l1e = src_l1[l1_idx];
            if ((l1e & 0x3) != 0x3) {
                ok = 0;
                break;
            }
            uint32_t l2_phys = (uint32_t)(l1e & 0xFFFFF000ULL);
            uint64_t *l2 = (uint64_t *)(uintptr_t)(l2_phys + 0x80000000U);
            uint64_t l2e = l2[l2_idx];
            if ((l2e & 0x3) != 0x3) {
                ok = 0;
                break;
            }
            uint32_t l3_phys = (uint32_t)(l2e & 0xFFFFF000ULL);
            uint64_t *l3 = (uint64_t *)(uintptr_t)(l3_phys + 0x80000000U);
            uint64_t l3e = l3[l3_idx];
            if (!(l3e & 0x1)) {
                ok = 0;
                break;
            }
            uint32_t phys = (uint32_t)(l3e & 0xFFFFF000ULL);
            mm_vmm_map_page(virt_start + i * 4096, phys,
                            PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER |
                                PAGE_FLAG_SHARED);
        }
        if (ok) {
            current_process->shared_virt_bump += page_count * 4096;
            regs->eax = virt_start + offset;
        } else {
            regs->eax = 0;
        }
        break;
    }
    case 64:
        regs->eax = (uint32_t)sys_waitpid((int)regs->ebx);
        break;
    case 65:
        regs->eax = (uint32_t)sys_pipe_create((const char *)(uintptr_t)regs->ebx);
        break;
    case 66:
        regs->eax = (uint32_t)sys_pipe_open((const char *)(uintptr_t)regs->ebx);
        break;
    case 67:
        regs->eax = (uint32_t)sys_pipe_write((int)regs->ebx,
                                             (const uint8_t *)(uintptr_t)regs->ecx, regs->edx);
        break;
    case 68:
        regs->eax = (uint32_t)sys_pipe_read((int)regs->ebx, (uint8_t *)(uintptr_t)regs->ecx,
                                            regs->edx);
        break;
    case 69:
        regs->eax = (uint32_t)sys_pipe_close((int)regs->ebx);
        break;
    case 70:
        regs->eax = (uint32_t)sys_set_alarm(regs->ebx);
        break;
    case 71:
        regs->eax = (uint32_t)sys_setenv((const char *)(uintptr_t)regs->ebx);
        break;
    case 72:
        regs->eax = (uint32_t)sys_getenv((const char *)(uintptr_t)regs->ebx,
                                         (char *)(uintptr_t)regs->ecx, regs->edx);
        break;
    case 73:
        regs->eax = (uint32_t)sys_signal_send((int)regs->ebx, (int)regs->ecx);
        break;
    case 74:
    {
        extern void virtio_gpu_flush(void);
        virtio_gpu_flush();
    }
        regs->eax = 0;
        break;
        static int blk_active_disk = 0;
    case 86: {
        blk_active_disk = (int)regs->ebx;
        regs->eax = 0;
        break;
    }
    case 80: {
        extern int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count, void *buf,
                                            int write);
        extern int virtio_blk_get_disk_count(void);
        extern void arm_uart_puts(const char *);
        arm_uart_puts("BLK80 ");
        if (blk_active_disk >= virtio_blk_get_disk_count()) {
            arm_uart_puts("NODEV\n");
            regs->eax = (uint32_t)-1;
            break;
        }
        uint32_t sector = regs->ebx;
        uint32_t count = regs->ecx;
        void *buf = (void *)(uintptr_t)regs->edx;
        if (!buf || count == 0 || count > 128 || !is_ptr_safe(buf, count * 512)) {
            regs->eax = (uint32_t)-1;
            break;
        }
        static uint8_t blk_bounce_r[512 * 8] __attribute__((aligned(64)));
        uint32_t done = 0;
        int ok = 1;
        while (done < count && ok) {
            uint32_t chunk = count - done;
            if (chunk > 8)
                chunk = 8;
            if (virtio_blk_transfer_disk(blk_active_disk, (uint64_t)(sector + done), chunk,
                                         blk_bounce_r, 0) != 0)
                ok = 0;
            else {
                memcpy((uint8_t *)buf + done * 512, blk_bounce_r, chunk * 512);
                done += chunk;
            }
        }
        regs->eax = ok ? 0 : (uint32_t)-1;
        arm_uart_puts(ok ? "OK\n" : "FAIL\n");
        break;
    }
    case 81: {
        extern int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count, void *buf,
                                            int write);
        extern int virtio_blk_get_disk_count(void);
        if (blk_active_disk >= virtio_blk_get_disk_count()) {
            regs->eax = (uint32_t)-1;
            break;
        }
        uint32_t sector = regs->ebx;
        uint32_t count = regs->ecx;
        const void *buf = (const void *)(uintptr_t)regs->edx;
        if (!buf || count == 0 || count > 128 || !is_ptr_safe(buf, count * 512)) {
            regs->eax = (uint32_t)-1;
            break;
        }
        static uint8_t blk_bounce_w[512 * 8] __attribute__((aligned(64)));
        uint32_t done = 0;
        int ok = 1;
        while (done < count && ok) {
            uint32_t chunk = count - done;
            if (chunk > 8)
                chunk = 8;
            memcpy(blk_bounce_w, (const uint8_t *)buf + done * 512, chunk * 512);
            if (virtio_blk_transfer_disk(blk_active_disk, (uint64_t)(sector + done), chunk,
                                         blk_bounce_w, 1) != 0)
                ok = 0;
            else
                done += chunk;
        }
        regs->eax = ok ? 0 : (uint32_t)-1;
        break;
    }
    case 82: {
        extern int virtio_blk_get_disk_count(void);
        regs->eax = (uint32_t)virtio_blk_get_disk_count();
        break;
    }
    case 83: {
        extern int virtio_blk_get_disk_info(int idx, disk_info_t *out);
        int disk_idx = (int)regs->ebx;
        disk_info_t *info = (disk_info_t *)(uintptr_t)regs->ecx;
        if (!info || !is_ptr_safe(info, sizeof(disk_info_t))) {
            regs->eax = (uint32_t)-1;
            break;
        }
        disk_info_t tmp;
        int r = virtio_blk_get_disk_info(disk_idx, &tmp);
        if (r == 0)
            memcpy(info, &tmp, sizeof(disk_info_t));
        regs->eax = (uint32_t)r;
        break;
    }
    case 84: {
        extern int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count, void *buf,
                                            int write);
        int disk_idx = (int)regs->ebx;
        blk_io_req_t *req = (blk_io_req_t *)(uintptr_t)regs->ecx;
        if (!req || !is_ptr_safe(req, sizeof(blk_io_req_t))) {
            regs->eax = (uint32_t)-1;
            break;
        }
        blk_io_req_t r;
        memcpy(&r, req, sizeof(r));
        void *buf = r.buf;
        if (!buf || r.count == 0 || r.count > 128 || !is_ptr_safe(buf, r.count * 512)) {
            regs->eax = (uint32_t)-1;
            break;
        }
        static uint8_t blk_bounce_dr[512 * 8] __attribute__((aligned(64)));
        uint32_t done = 0;
        int ok = 1;
        while (done < r.count && ok) {
            uint32_t chunk = r.count - done;
            if (chunk > 8)
                chunk = 8;
            if (virtio_blk_transfer_disk(disk_idx, (uint64_t)(r.sector + done), chunk,
                                         blk_bounce_dr, 0) != 0)
                ok = 0;
            else {
                memcpy((uint8_t *)buf + done * 512, blk_bounce_dr, chunk * 512);
                done += chunk;
            }
        }
        regs->eax = ok ? 0 : (uint32_t)-1;
        break;
    }
    case 85: {
        extern int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count, void *buf,
                                            int write);
        int disk_idx = (int)regs->ebx;
        blk_io_req_t *req = (blk_io_req_t *)(uintptr_t)regs->ecx;
        if (!req || !is_ptr_safe(req, sizeof(blk_io_req_t))) {
            regs->eax = (uint32_t)-1;
            break;
        }
        blk_io_req_t r;
        memcpy(&r, req, sizeof(r));
        const void *buf = r.buf;
        if (!buf || r.count == 0 || r.count > 128 || !is_ptr_safe(buf, r.count * 512)) {
            regs->eax = (uint32_t)-1;
            break;
        }
        static uint8_t blk_bounce_dw[512 * 8] __attribute__((aligned(64)));
        uint32_t done = 0;
        int ok = 1;
        while (done < r.count && ok) {
            uint32_t chunk = r.count - done;
            if (chunk > 8)
                chunk = 8;
            memcpy(blk_bounce_dw, (const uint8_t *)buf + done * 512, chunk * 512);
            if (virtio_blk_transfer_disk(disk_idx, (uint64_t)(r.sector + done), chunk,
                                         blk_bounce_dw, 1) != 0)
                ok = 0;
            else
                done += chunk;
        }
        regs->eax = ok ? 0 : (uint32_t)-1;
        break;
    }
    case 90: {
        extern void arm_uart_puts(const char *);
        const char *msg = (const char *)(uintptr_t)regs->ebx;
        if (msg && is_ptr_safe(msg, 1)) {
            arm_uart_puts(msg);
        }
        regs->eax = 0;
        break;
    }
    default:
        regs->eax = -1;
        break;
    }
}
