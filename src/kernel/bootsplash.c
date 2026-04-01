#include "kernel/bootsplash.h"

#include "drivers/GUI/framebuffer.h"
#include "gfx/bmp.h"
#include "lib/stdlib.h"
#include "lib/stdbool.h"
#include "mm/memory.h"
#include "util/util.h"

#define BOOT_LOG_MAX_LINES 64
#define BOOT_LOG_LINE_LEN  128
#define BOOT_LOG_DEFAULT_DELTA_US 150000

typedef struct {
    uint16_t x, y, w, h;
    int16_t advance, off_x, off_y;
} __attribute__((packed)) K_GlyphInfo;

typedef struct {
    uint32_t magic;
    uint32_t tex_width, tex_height, line_height, ascent, count;
    K_GlyphInfo *glyphs;
    uint32_t *bitmap;
} K_AtlasFont;

extern uint32_t k_font_size;
extern uint32_t k_font_phys_addr;
extern void* k_font_virt_addr;
extern uint32_t k_logo_phys_addr;
extern uint32_t k_logo_size;

static char boot_log_lines[BOOT_LOG_MAX_LINES][BOOT_LOG_LINE_LEN];
static const char *boot_log_entries[BOOT_LOG_MAX_LINES];
static uint64_t boot_log_timestamps[BOOT_LOG_MAX_LINES];
static uint32_t boot_log_count = 0;
static uint64_t boot_log_time_us = 0;
static bool boot_time_ready = false;
static uint64_t boot_time_tsc_start = 0;
static uint64_t boot_time_ticks_per_us = 0;
static bool boot_cpuid_cached = false;
static uint32_t boot_cpuid_max_basic = 0;
static uint32_t boot_cpuid_max_ext = 0;
static char boot_cpu_vendor[13];
static char boot_cpu_brand[48];
static bool boot_cpu_brand_valid = false;
static K_AtlasFont k_boot_font;

static void boot_log_line_append(char *dest, const char *text);
static inline uint64_t boot_read_tsc(void);
static uint64_t boot_parse_decimal_to_hz(const char *start, const char *end, uint64_t multiplier);
static uint64_t boot_extract_freq_value(const char *brand, const char *unit_pos, uint64_t multiplier);
static void boot_cpuid_cache(void);
static uint64_t boot_brand_freq_from_string(const char *brand);
static void boot_time_ensure_initialized(void);
static uint64_t boot_time_now_us(void);
static uint64_t boot_log_next_timestamp(void);
static void cpuid_get_vendor(char vendor_out[13], uint32_t *max_basic, uint32_t *max_ext);
static bool boot_detect_cpu_brand(char *out, uint32_t len, uint32_t max_ext);
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d);

static inline uint64_t boot_read_tsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t boot_parse_decimal_to_hz(const char *start, const char *end, uint64_t multiplier) {
    if (!start || !end || start >= end) return 0;
    while (start < end && *start == ' ') start++;
    bool saw_digit = false;
    uint64_t integer = 0;
    while (start < end && *start >= '0' && *start <= '9') {
        saw_digit = true;
        integer = integer * 10 + (uint64_t)(*start - '0');
        start++;
    }
    uint64_t frac = 0;
    uint64_t scale = 1;
    if (start < end && *start == '.') {
        start++;
        while (start < end && *start >= '0' && *start <= '9' && scale < 1000000ULL) {
            saw_digit = true;
            frac = frac * 10 + (uint64_t)(*start - '0');
            scale *= 10;
            start++;
        }
    }
    if (!saw_digit) return 0;
    uint64_t hz = integer * multiplier;
    if (scale > 1) hz += (frac * multiplier) / scale;
    return hz;
}

static uint64_t boot_extract_freq_value(const char *brand, const char *unit_pos, uint64_t multiplier) {
    if (!brand || !unit_pos) return 0;
    const char *end = unit_pos;
    while (end > brand && (*(end - 1) == ' ' || *(end - 1) == '@')) end--;
    const char *start = end;
    while (start > brand) {
        char c = *(start - 1);
        if ((c >= '0' && c <= '9') || c == '.' || c == ' ') {
            start--;
        } else {
            break;
        }
    }
    return boot_parse_decimal_to_hz(start, end, multiplier);
}

static uint64_t boot_brand_freq_from_string(const char *brand) {
    if (!brand) return 0;
    const char *unit = strstr(brand, "GHz");
    if (unit) {
        uint64_t hz = boot_extract_freq_value(brand, unit, 1000000000ULL);
        if (hz) return hz;
    }
    unit = strstr(brand, "MHz");
    if (unit) {
        uint64_t hz = boot_extract_freq_value(brand, unit, 1000000ULL);
        if (hz) return hz;
    }
    return 0;
}

static void boot_cpuid_cache(void) {
    if (boot_cpuid_cached) return;
    cpuid_get_vendor(boot_cpu_vendor, &boot_cpuid_max_basic, &boot_cpuid_max_ext);
    boot_cpu_brand_valid = boot_detect_cpu_brand(boot_cpu_brand, sizeof(boot_cpu_brand), boot_cpuid_max_ext);
    boot_cpuid_cached = true;
}

static void boot_time_ensure_initialized(void) {
    if (boot_time_ready) return;
    boot_cpuid_cache();
    uint64_t freq_hz = 0;
    if (boot_cpuid_max_basic >= 0x15) {
        uint32_t eax, ebx, ecx, edx;
        cpuid(0x15u, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0 && ebx != 0) {
            uint64_t crystal = ecx ? (uint64_t)ecx : 24000000ULL;
            freq_hz = (crystal * (uint64_t)ebx) / (uint64_t)eax;
        }
    }
    if (freq_hz == 0 && boot_cpuid_max_basic >= 0x16) {
        uint32_t eax, ebx, ecx, edx;
        cpuid(0x16u, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0) {
            freq_hz = (uint64_t)eax * 1000000ULL;
        }
    }
    if (freq_hz == 0 && boot_cpu_brand_valid) {
        freq_hz = boot_brand_freq_from_string(boot_cpu_brand);
    }
    if (freq_hz == 0) freq_hz = 1000000000ULL;
    boot_time_ticks_per_us = freq_hz / 1000000ULL;
    if (boot_time_ticks_per_us == 0) boot_time_ticks_per_us = 1;
    boot_time_tsc_start = boot_read_tsc();
    boot_time_ready = true;
}

static uint64_t boot_time_now_us(void) {
    if (!boot_time_ready || boot_time_ticks_per_us == 0) return boot_log_time_us;
    uint64_t now = boot_read_tsc();
    if (now < boot_time_tsc_start) return 0;
    uint64_t delta = now - boot_time_tsc_start;
    return delta / boot_time_ticks_per_us;
}

static uint64_t boot_log_next_timestamp(void) {
    if (!boot_time_ready) boot_time_ensure_initialized();
    if (boot_time_ready) return boot_time_now_us();
    uint64_t ts = boot_log_time_us;
    boot_log_time_us += BOOT_LOG_DEFAULT_DELTA_US;
    return ts;
}

static void boot_log_format_timestamp(char *dest, uint64_t usec) {
    char digits[32];
    int len = 0;
    if (usec == 0) {
        digits[len++] = '0';
    } else {
        while (usec > 0 && len < (int)sizeof(digits)) {
            digits[len++] = '0' + (usec % 10);
            usec /= 10;
        }
        for (int i = 0; i < len / 2; ++i) {
            char tmp = digits[i];
            digits[i] = digits[len - 1 - i];
            digits[len - 1 - i] = tmp;
        }
    }

    uint32_t idx = 0;
    dest[idx++] = '[';

    int int_len = (len > 6) ? (len - 6) : 1;
    if (len > 6) {
        for (int i = 0; i < int_len; ++i) dest[idx++] = digits[i];
    } else {
        dest[idx++] = '0';
    }

    dest[idx++] = '.';

    int frac_start = (len > 6) ? int_len : 0;
    int frac_digits = len - frac_start;
    int frac_written = 0;
    int leading_zeroes = 6 - frac_digits;
    for (int i = 0; i < leading_zeroes; ++i) {
        dest[idx++] = '0';
        frac_written++;
    }
    for (int i = frac_start; i < len && frac_written < 6; ++i) {
        dest[idx++] = digits[i];
        frac_written++;
    }
    while (frac_written < 6) {
        dest[idx++] = '0';
        frac_written++;
    }

    dest[idx++] = ']';
    dest[idx++] = ' ';
    dest[idx] = '\0';
}

static void boot_log_render_entry(int index, const char *message, uint64_t timestamp) {
    if (index < 0 || index >= (int)BOOT_LOG_MAX_LINES) return;
    char *line = boot_log_lines[index];
    boot_log_format_timestamp(line, timestamp);
    boot_log_line_append(line, message);
}

static void boot_log_reset(void) {
    boot_log_count = 0;
    boot_log_time_us = 0;
}

int boot_log_add(const char *message) {
    if (boot_log_count >= BOOT_LOG_MAX_LINES)
        return -1;
    uint64_t timestamp = boot_log_next_timestamp();
    boot_log_timestamps[boot_log_count] = timestamp;
    boot_log_render_entry(boot_log_count, message, timestamp);
    boot_log_entries[boot_log_count] = boot_log_lines[boot_log_count];
    return (int)boot_log_count++;
}

void boot_log_replace(int index, const char *message) {
    if (index < 0 || index >= (int)boot_log_count) return;
    boot_log_render_entry(index, message, boot_log_timestamps[index]);
}

static void boot_log_line_append(char *dest, const char *text) {
    if (!text) return;
    uint32_t dest_len = strlen(dest);
    if (dest_len >= BOOT_LOG_LINE_LEN - 1) return;
    uint32_t remaining = (BOOT_LOG_LINE_LEN - 1) - dest_len;
    uint32_t i = 0;
    while (text[i] && i < remaining) {
        dest[dest_len + i] = text[i];
        i++;
    }
    dest[dest_len + i] = '\0';
}

static void boot_log_add_pair(const char *label, const char *value) {
    char line[BOOT_LOG_LINE_LEN];
    line[0] = '\0';
    boot_log_line_append(line, label);
    boot_log_line_append(line, value);
    boot_log_add(line);
}

void boot_log_add_dec(const char *label, uint32_t value, const char *suffix) {
    char line[BOOT_LOG_LINE_LEN];
    char number[16];
    line[0] = '\0';
    boot_log_line_append(line, label);
    int_to_str((int)value, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, suffix);
    boot_log_add(line);
}

void boot_log_add_hex(const char *label, uint32_t value) {
    char line[BOOT_LOG_LINE_LEN];
    char hex[11];
    uint_to_hex(value, hex);
    line[0] = '\0';
    boot_log_line_append(line, label);
    boot_log_line_append(line, hex);
    boot_log_add(line);
}

static bool boot_detect_cpu_brand(char *out, uint32_t len, uint32_t max_ext) {
    if (max_ext < 0x80000004u || len < 48) {
        if (len) out[0] = '\0';
        return false;
    }
    uint32_t eax, ebx, ecx, edx;
    for (uint32_t i = 0; i < 3; i++) {
        cpuid(0x80000002u + i, 0, &eax, &ebx, &ecx, &edx);
        memcpy(out + i * 16 + 0, &eax, 4);
        memcpy(out + i * 16 + 4, &ebx, 4);
        memcpy(out + i * 16 + 8, &ecx, 4);
        memcpy(out + i * 16 + 12, &edx, 4);
    }
    out[47] = '\0';
    return true;
}

static void boot_log_add_cpu_signature(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    uint32_t family = (eax >> 8) & 0xF;
    uint32_t model = (eax >> 4) & 0xF;
    uint32_t stepping = eax & 0xF;
    uint32_t ext_family = (eax >> 20) & 0xFF;
    uint32_t ext_model = (eax >> 16) & 0xF;
    if (family == 0xF) family += ext_family;
    if (family == 0x6 || family == 0xF) model |= (ext_model << 4);

    char line[BOOT_LOG_LINE_LEN];
    char number[16];
    line[0] = '\0';
    boot_log_line_append(line, "CPU ID: family ");
    int_to_str((int)family, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, ", model ");
    int_to_str((int)model, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, ", stepping ");
    int_to_str((int)stepping, number, 0);
    boot_log_line_append(line, number);
    boot_log_add(line);
}

static void boot_log_add_memory_info(struct multiboot_info *boot_info) {
    if (!boot_info || !(boot_info->flags & 0x1)) return;
    char line[BOOT_LOG_LINE_LEN];
    char number[16];
    line[0] = '\0';
    boot_log_line_append(line, "Memory: lower ");
    int_to_str((int)boot_info->mem_lower, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, " KB, upper ");
    int_to_str((int)boot_info->mem_upper, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, " KB");
    boot_log_add(line);
}

static void boot_log_add_cpu_features(uint32_t max_basic, uint32_t max_ext) {
    if (max_basic < 1) return;
    uint32_t eax1, ebx1, ecx1, edx1;
    cpuid(1, 0, &eax1, &ebx1, &ecx1, &edx1);
    uint32_t ecx8 = 0, edx8 = 0;
    if (max_ext >= 0x80000001u) {
        uint32_t eax8, ebx8;
        cpuid(0x80000001u, 0, &eax8, &ebx8, &ecx8, &edx8);
    }

    typedef struct {
        const char *name;
        uint8_t reg;
        uint8_t bit;
    } feature_bit_t;

    enum { REG_ECX1, REG_EDX1, REG_ECX8, REG_EDX8 };

    static const feature_bit_t feature_bits[] = {
        {"MMX",   REG_EDX1, 23},
        {"SSE",   REG_EDX1, 25},
        {"SSE2",  REG_EDX1, 26},
        {"PAE",   REG_EDX1, 6},
        {"APIC",  REG_EDX1, 9},
        {"SSE3",  REG_ECX1, 0},
        {"SSSE3", REG_ECX1, 9},
        {"SSE4.1",REG_ECX1, 19},
        {"SSE4.2",REG_ECX1, 20},
        {"AVX",   REG_ECX1, 28},
        {"RDRAND",REG_ECX1, 30},
        {"VMX",   REG_ECX1, 5},
        {"NX",    REG_EDX8, 20},
        {"SVM",   REG_ECX8, 2},
    };

    char line[BOOT_LOG_LINE_LEN];
    line[0] = '\0';
    boot_log_line_append(line, "CPU Features:");
    int count = 0;
    for (uint32_t i = 0; i < sizeof(feature_bits) / sizeof(feature_bits[0]); i++) {
        const feature_bit_t *fb = &feature_bits[i];
        bool present = false;
        switch (fb->reg) {
            case REG_ECX1: present = (ecx1 & (1u << fb->bit)) != 0; break;
            case REG_EDX1: present = (edx1 & (1u << fb->bit)) != 0; break;
            case REG_ECX8: present = (ecx8 & (1u << fb->bit)) != 0; break;
            case REG_EDX8: present = (edx8 & (1u << fb->bit)) != 0; break;
        }
        if (present) {
            boot_log_line_append(line, " ");
            boot_log_line_append(line, fb->name);
            count++;
        }
    }
    if (count == 0) boot_log_line_append(line, " none");
    boot_log_add(line);

    char virt_line[BOOT_LOG_LINE_LEN];
    virt_line[0] = '\0';
    boot_log_line_append(virt_line, "Virtualization: ");
    bool virt_flag = false;
    if (ecx1 & (1u << 5)) { boot_log_line_append(virt_line, "VMX"); virt_flag = true; }
    if (max_ext >= 0x80000001u && ((ecx8 & (1u << 2)) != 0)) {
        if (virt_flag) boot_log_line_append(virt_line, " ");
        boot_log_line_append(virt_line, "SVM");
        virt_flag = true;
    }
    if (!virt_flag) boot_log_line_append(virt_line, "none");
    boot_log_add(virt_line);

    if (ecx1 & (1u << 31)) {
        char hv[13];
        if (max_basic >= 0x40000000u) {
            uint32_t eaxh, ebxh, ecxh, edxh;
            cpuid(0x40000000u, 0, &eaxh, &ebxh, &ecxh, &edxh);
            memcpy(hv + 0, &ebxh, 4);
            memcpy(hv + 4, &ecxh, 4);
            memcpy(hv + 8, &edxh, 4);
            hv[12] = '\0';
        } else {
            hv[0] = '\0';
        }
        char hv_line[BOOT_LOG_LINE_LEN];
        hv_line[0] = '\0';
        boot_log_line_append(hv_line, "Hypervisor: ");
        if (hv[0]) boot_log_line_append(hv_line, hv);
        else boot_log_line_append(hv_line, "present");
        boot_log_add(hv_line);
    } else {
        boot_log_add("Hypervisor: none detected");
    }
}

void boot_log_add_framebuffer_info(void) {
    if (fb_width == 0 || fb_height == 0) return;
    char line[BOOT_LOG_LINE_LEN];
    char number[16];
    line[0] = '\0';
    boot_log_line_append(line, "Framebuffer: ");
    int_to_str((int)fb_width, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, "x");
    int_to_str((int)fb_height, number, 0);
    boot_log_line_append(line, number);
    boot_log_line_append(line, ", ");
    int_to_str((int)fb_bpp, number, 0);
    boot_log_line_append(line, " bpp");
    boot_log_add(line);
}

void boot_log_add_rtc_line(const Time *t) {
    if (!t) return;
    char line[BOOT_LOG_LINE_LEN];
    char hh[4], mm[4], ss[4], day[4], month[4], year[8];
    time_to_str(t->hour, hh);
    time_to_str(t->minute, mm);
    time_to_str(t->second, ss);
    time_to_str(t->day, day);
    time_to_str(t->month, month);
    int_to_str(t->year, year, 0);

    line[0] = '\0';
    boot_log_line_append(line, "RTC: ");
    boot_log_line_append(line, hh);
    boot_log_line_append(line, ":");
    boot_log_line_append(line, mm);
    boot_log_line_append(line, ":");
    boot_log_line_append(line, ss);
    boot_log_line_append(line, ", date ");
    boot_log_line_append(line, year);
    boot_log_line_append(line, "-");
    boot_log_line_append(line, month);
    boot_log_line_append(line, "-");
    boot_log_line_append(line, day);
    boot_log_add(line);
}

void boot_log_add_acpi_capabilities(bool power_button, bool reset_capable_flag) {
    char line[BOOT_LOG_LINE_LEN];
    line[0] = '\0';
    boot_log_line_append(line, "ACPI S5 poweroff: ");
    boot_log_line_append(line, power_button ? "available" : "missing");
    boot_log_add(line);

    char reset_line[BOOT_LOG_LINE_LEN];
    reset_line[0] = '\0';
    boot_log_line_append(reset_line, "ACPI reset register: ");
    boot_log_line_append(reset_line, reset_capable_flag ? "available" : "missing");
    boot_log_add(reset_line);
}

void boot_log_record_boot_info(struct multiboot_info *boot_info) {
    boot_log_reset();
    boot_cpuid_cache();
    boot_log_add("Blanket core 0.3.1 (x86_32) booting...");

    if (boot_info && (boot_info->flags & (1 << 2)) && boot_info->cmdline) {
        const char *cmdline = (const char *)(uintptr_t)boot_info->cmdline;
        boot_log_add_pair("Command line: ", cmdline);
    }

    boot_log_add_pair("CPU Vendor: ", boot_cpu_vendor);

    if (boot_cpu_brand_valid) {
        boot_log_add_pair("Detected CPU: ", boot_cpu_brand);
    } else {
        char fallback[BOOT_LOG_LINE_LEN];
        fallback[0] = '\0';
        boot_log_line_append(fallback, "Detected CPU: ");
        boot_log_line_append(fallback, boot_cpu_vendor);
        boot_log_line_append(fallback, " (brand string unavailable)");
        boot_log_add(fallback);
    }
    boot_log_add_cpu_signature();
    boot_log_add_cpu_features(boot_cpuid_max_basic, boot_cpuid_max_ext);
    boot_log_add_memory_info(boot_info);
    if (boot_info) {
        boot_log_add_dec("Boot modules: ", boot_info->mods_count, " loaded");
    }
}

static void k_font_init(void* data) {
    if (!data) return;
    uint32_t* ptr = (uint32_t*)data;
    k_boot_font.magic = ptr[0];
    k_boot_font.tex_width = ptr[1];
    k_boot_font.tex_height = ptr[2];
    k_boot_font.line_height = ptr[3];
    k_boot_font.ascent = ptr[4];
    k_boot_font.count = ptr[5];
    k_boot_font.glyphs = (K_GlyphInfo*)(ptr + 6);
    k_boot_font.bitmap = (uint32_t*)((uint8_t*)k_boot_font.glyphs + k_boot_font.count * sizeof(K_GlyphInfo));
}

static int k_draw_string_atlas(int x, int y, const char* str, uint32_t color) {
    if (!k_font_virt_addr) return x;
    while (*str) {
        char c = *str++;
        if (c < 32 || c > 126) continue;
        K_GlyphInfo* g = &k_boot_font.glyphs[c - 32];
        for (int iy = 0; iy < (int)g->h; iy++) {
            for (int ix = 0; ix < (int)g->w; ix++) {
                uint32_t px = k_boot_font.bitmap[(g->y + iy) * k_boot_font.tex_width + (g->x + ix)];
                uint8_t a = (px >> 24) & 0xFF;
                if (a > 0) put_pixel_AA(x + g->off_x + ix, y + g->off_y + iy, color, a);
            }
        }
        x += g->advance;
    }
    return x;
}

static void k_draw_boot_line(int x, int y, const char* str) {
    const char* bracket_end = str;
    while (*bracket_end && *bracket_end != ']') bracket_end++;
    uint32_t text_color = 0x888888;
    if (strstr(str, "NOTICE")) text_color = COLOR_WHITE;
    else if (strstr(str, "INFO")) text_color = 0x44CC44;
    if (*bracket_end == ']') {
        char prefix[32];
        int len = (bracket_end - str) + 1;
        if (len > 31) len = 31;
        strncpy(prefix, str, len);
        prefix[len] = 0;
        int next_x = k_draw_string_atlas(x, y, prefix, 0x555555);
        k_draw_string_atlas(next_x, y, bracket_end + 1, text_color);
    } else {
        k_draw_string_atlas(x, y, str, text_color);
    }
}

void draw_boot_splash() {
    if (k_logo_phys_addr == 0 || !k_font_virt_addr) return;
    void* logo_data = map_module(k_logo_phys_addr, k_logo_size);
    k_font_init(k_font_virt_addr);
    BITMAPINFOHEADER* info = (BITMAPINFOHEADER*)(logo_data + sizeof(BITMAPFILEHEADER));
    int bar_w = 200, bar_h = 2, gap = 70;
    int scaled_w = info->bi_width, scaled_h = info->bi_height;
    int logo_x = (fb_width - scaled_w) / 2, logo_y = (fb_height - (scaled_h + gap + bar_h)) / 2;
    int bar_x = (fb_width - bar_w) / 2, bar_y = logo_y + scaled_h + gap;
    int last_line = -1;
    for (int i = 0; i <= 100; i++) {
        clear_screen(COLOR_BLACK);
        draw_bmp(logo_data, logo_x, logo_y);
        rect_fill(bar_x, bar_y, bar_w, bar_h, 0x1A1A1A);
        rect_fill(bar_x, bar_y, (bar_w * i) / 100, bar_h, COLOR_WHITE);
        int current_line_limit = (i * (int)boot_log_count) / 100;
        for (int m = 0; m <= current_line_limit && m < (int)boot_log_count; m++) {
            k_draw_boot_line(15, 20 + (m * (k_boot_font.line_height - 2)), boot_log_entries[m]);
        }
        drv_fb_swap_buffers();
        int delay_multiplier = 1;
        if (current_line_limit != last_line) {
            if (current_line_limit == 4 || current_line_limit == 12 ||
                current_line_limit == 18 || current_line_limit == 26 ||
                current_line_limit == 30) delay_multiplier = 15;
            last_line = current_line_limit;
        }
        for (volatile int d = 0; d < 800000 * delay_multiplier; d++) __asm__("pause");
    }
}

static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid"
                 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                 : "a"(leaf), "c"(subleaf));
}

static void cpuid_get_vendor(char vendor_out[13], uint32_t *max_basic, uint32_t *max_ext) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (max_basic) *max_basic = eax;
    memcpy(vendor_out + 0, &ebx, 4);
    memcpy(vendor_out + 4, &edx, 4);
    memcpy(vendor_out + 8, &ecx, 4);
    vendor_out[12] = '\0';
    cpuid(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    if (max_ext) *max_ext = eax;
}
