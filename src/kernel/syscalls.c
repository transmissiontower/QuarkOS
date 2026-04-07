#include "kernel/syscalls.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/acpi.h"
#include "drivers/pcspk.h"
#include "drivers/rtc.h"
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
    return 0;
}

int sys_get_server_pid(int type)
{
    if (type == 0)
        return storage_server_pid;
    if (type == 1)
        return compositor_server_pid;
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
    (void)code;
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
    k_proc_spawn((uint8_t *)buffer, size);
    return 0;
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
        if (dest->state == PROCESS_IPC_WAIT)
            dest->state = PROCESS_READY;
        return 0;
    }

    if (dest->ipc_wait_from != -1)
        return 0;

    while (dest->ipc_queue.count >= IPC_QUEUE_SIZE)
        k_scheduler_yield();
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
    if (dest->state == PROCESS_IPC_WAIT)
        k_scheduler_direct_switch(dest);
    else {
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
    /* 52: pcspk_pwm_play(buf, len, rate) — start one PWM chunk */
    case 52:
        drv_pcspk_pwm_start((const uint8_t *)regs->ebx,
                             (uint32_t)regs->ecx,
                             (uint32_t)regs->edx);
        regs->eax = 0;
        break;
    /* 53: pcspk_pwm_ready() — returns 1 when current chunk is done */
    case 53:
        regs->eax = drv_pcspk_pwm_ready();
        break;
    default:
        regs->eax = -1;
        break;
    }
}
