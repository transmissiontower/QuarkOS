#include "drivers/video/bootsplash.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/bmp.h"
#include "stdbool.h"
#include "stdlib.h"
#include "kernel/mm/memory.h"
#include "kernel/lib/util.h"
#define BOOT_LOG_MAX_LINES 64
#define BOOT_LOG_LINE_LEN 128
#define BOOT_LOG_DEFAULT_DELTA_US 150000
typedef struct {
    uint32_t codepoint;
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
extern void *k_font_virt_addr;
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
static bool boot_cpu_identified = false;
static char boot_cpu_vendor[13];
static char boot_cpu_brand[48];
static bool boot_cpu_brand_valid = false;
static K_AtlasFont k_boot_font;
static void boot_log_line_append(char *dest, const char *text);
static inline uint64_t boot_read_tsc(void);
static void boot_time_ensure_initialized(void);
static uint64_t boot_time_now_us(void);
static uint64_t boot_log_next_timestamp(void);
static inline uint64_t boot_read_tsc(void) {
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static void boot_cpu_identify(void) {
    if (boot_cpu_identified)
        return;
    {
        uint64_t midr;
        asm volatile("mrs %0, midr_el1" : "=r"(midr));
        uint32_t implementer = ((uint32_t)midr >> 24) & 0xFF;
        uint32_t partnum = ((uint32_t)midr >> 4) & 0xFFF;
        uint32_t variant = ((uint32_t)midr >> 20) & 0xF;
        uint32_t revision = (uint32_t)midr & 0xF;

        const char *impl_name = "Unknown";
        switch (implementer) {
        case 0x41:
            impl_name = "ARM Limited";
            break;
        case 0x42:
            impl_name = "Broadcom";
            break;
        case 0x43:
            impl_name = "Cavium";
            break;
        case 0x44:
            impl_name = "DEC";
            break;
        case 0x46:
            impl_name = "Fujitsu";
            break;
        case 0x48:
            impl_name = "HiSilicon";
            break;
        case 0x4E:
            impl_name = "NVIDIA";
            break;
        case 0x50:
            impl_name = "APM";
            break;
        case 0x51:
            impl_name = "Qualcomm";
            break;
        case 0x53:
            impl_name = "Samsung";
            break;
        case 0x56:
            impl_name = "Marvell";
            break;
        case 0x61:
            impl_name = "Apple";
            break;
        case 0x69:
            impl_name = "Intel";
            break;
        case 0xC0:
            impl_name = "Ampere";
            break;
        }
        int vi = 0;
        while (impl_name[vi] && vi < 12) {
            boot_cpu_vendor[vi] = impl_name[vi];
            vi++;
        }
        boot_cpu_vendor[vi] = 0;

        const char *part_name = "Unknown";
        if (implementer == 0x41) {
            switch (partnum) {
            case 0xD03:
                part_name = "Cortex-A53";
                break;
            case 0xD07:
                part_name = "Cortex-A57";
                break;
            case 0xD08:
                part_name = "Cortex-A72";
                break;
            case 0xD09:
                part_name = "Cortex-A73";
                break;
            case 0xD0A:
                part_name = "Cortex-A75";
                break;
            case 0xD0B:
                part_name = "Cortex-A76";
                break;
            case 0xD0D:
                part_name = "Cortex-A77";
                break;
            case 0xD40:
                part_name = "Neoverse-V1";
                break;
            case 0xD41:
                part_name = "Cortex-A78";
                break;
            case 0xD44:
                part_name = "Cortex-X1";
                break;
            case 0xD46:
                part_name = "Cortex-A510";
                break;
            case 0xD47:
                part_name = "Cortex-A710";
                break;
            case 0xD4D:
                part_name = "Cortex-A715";
                break;
            default:
                part_name = "ARM Core";
                break;
            }
        } else if (implementer == 0x61) {
            switch (partnum) {
            case 0x022:
                part_name = "Apple M1 Icestorm";
                break;
            case 0x023:
                part_name = "Apple M1 Firestorm";
                break;
            case 0x024:
                part_name = "Apple M1 Pro";
                break;
            case 0x025:
                part_name = "Apple M1 Pro";
                break;
            case 0x028:
                part_name = "Apple M1 Max";
                break;
            case 0x029:
                part_name = "Apple M1 Max";
                break;
            case 0x030:
                part_name = "Apple M2 Blizzard";
                break;
            case 0x031:
                part_name = "Apple M2 Avalanche";
                break;
            case 0x032:
                part_name = "Apple M2 Pro";
                break;
            case 0x033:
                part_name = "Apple M2 Pro";
                break;
            case 0x034:
                part_name = "Apple M2 Max";
                break;
            case 0x035:
                part_name = "Apple M2 Max";
                break;
            case 0x038:
                part_name = "Apple M3 Blizzard";
                break;
            case 0x039:
                part_name = "Apple M3 Avalanche";
                break;
            case 0x049:
                part_name = "Apple M4 Sawtooth";
                break;
            case 0x048:
                part_name = "Apple M4 Everest";
                break;
            default:
                part_name = "Apple Silicon";
                break;
            }
        }

        int bi = 0;
        const char *pn = part_name;
        while (*pn && bi < 40)
            boot_cpu_brand[bi++] = *pn++;
        boot_cpu_brand[bi++] = ' ';
        boot_cpu_brand[bi++] = 'r';
        boot_cpu_brand[bi++] = '0' + (variant & 0xF);
        boot_cpu_brand[bi++] = 'p';
        boot_cpu_brand[bi++] = '0' + (revision & 0xF);
        boot_cpu_brand[bi] = 0;
        boot_cpu_brand_valid = true;

    }
    boot_cpu_identified = true;
}
static void boot_time_ensure_initialized(void) {
    if (boot_time_ready)
        return;
    boot_cpu_identify();
    uint64_t freq_hz = 0;
    {
        uint64_t cntfrq;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
        freq_hz = cntfrq;
    }
    if (freq_hz == 0)
        freq_hz = 1000000000ULL;
    boot_time_ticks_per_us = freq_hz / 1000000ULL;
    if (boot_time_ticks_per_us == 0)
        boot_time_ticks_per_us = 1;
    boot_time_tsc_start = boot_read_tsc();
    boot_time_ready = true;
}
static uint64_t boot_time_now_us(void) {
    if (!boot_time_ready || boot_time_ticks_per_us == 0)
        return boot_log_time_us;
    uint64_t now = boot_read_tsc();
    if (now < boot_time_tsc_start)
        return 0;
    uint64_t delta = now - boot_time_tsc_start;
    return delta / boot_time_ticks_per_us;
}
static uint64_t boot_log_next_timestamp(void) {
    if (!boot_time_ready)
        boot_time_ensure_initialized();
    if (boot_time_ready)
        return boot_time_now_us();
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
        for (int i = 0; i < int_len; ++i)
            dest[idx++] = digits[i];
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
    if (index < 0 || index >= (int)BOOT_LOG_MAX_LINES)
        return;
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
    if (index < 0 || index >= (int)boot_log_count)
        return;
    boot_log_render_entry(index, message, boot_log_timestamps[index]);
}
static void boot_log_line_append(char *dest, const char *text) {
    if (!text)
        return;
    uint32_t dest_len = strlen(dest);
    if (dest_len >= BOOT_LOG_LINE_LEN - 1)
        return;
    uint32_t remaining = (BOOT_LOG_LINE_LEN - 1) - dest_len;
    uint32_t i = 0;
    while (text[i] && i < remaining) {
        dest[dest_len + i] = text[i];
        i++;
    }
    dest[dest_len + i] = '\0';
}
static void int_to_hex_str(uint32_t val, char *out) {
    int i = 0;
    char tmp[9];
    if (val == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (val > 0 && i < 8) {
        int n = val & 0xF;
        tmp[i++] = (n < 10) ? '0' + n : 'A' + n - 10;
        val >>= 4;
    }
    int j = 0;
    while (--i >= 0)
        out[j++] = tmp[i];
    out[j] = 0;
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
static void boot_log_add_memory_info(const boot_info_t *boot_info) {
    if (!boot_info)
        return;
    boot_log_add_dec("RAM: ", boot_info->ram_size / (1024 * 1024), " MB");
}
void boot_log_add_framebuffer_info(void) {
    if (fb_width == 0 || fb_height == 0)
        return;
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
    if (!t)
        return;
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
void boot_log_record_boot_info(boot_info_t *boot_info) {
    boot_log_reset();
    boot_cpu_identify();
    boot_log_add("Sodium (AArch64) booting...");
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
    {
        uint64_t midr;
        asm volatile("mrs %0, midr_el1" : "=r"(midr));
        char line[BOOT_LOG_LINE_LEN];
        char number[16];
        line[0] = '\0';
        boot_log_line_append(line, "MIDR_EL1: implementer 0x");
        int_to_hex_str(((uint32_t)midr >> 24) & 0xFF, number);
        boot_log_line_append(line, number);
        boot_log_line_append(line, ", part 0x");
        int_to_hex_str(((uint32_t)midr >> 4) & 0xFFF, number);
        boot_log_line_append(line, number);
        boot_log_add(line);

        uint64_t isar0;
        asm volatile("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
        line[0] = '\0';
        boot_log_line_append(line, "CPU features:");
        if ((isar0 >> 4) & 0xF)
            boot_log_line_append(line, " AES");
        if ((isar0 >> 8) & 0xF)
            boot_log_line_append(line, " SHA1");
        if ((isar0 >> 12) & 0xF)
            boot_log_line_append(line, " SHA256");
        if ((isar0 >> 16) & 0xF)
            boot_log_line_append(line, " CRC32");
        if ((isar0 >> 20) & 0xF)
            boot_log_line_append(line, " ATOMICS");
        if ((isar0 >> 28) & 0xF)
            boot_log_line_append(line, " SHA512");
        if ((isar0 >> 32) & 0xF)
            boot_log_line_append(line, " SHA3");
        if ((isar0 >> 44) & 0xF)
            boot_log_line_append(line, " FHM");
        if ((isar0 >> 52) & 0xF)
            boot_log_line_append(line, " RNG");
        boot_log_add(line);

        uint64_t cntfrq;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
        line[0] = '\0';
        boot_log_line_append(line, "Timer frequency: ");
        int_to_str((int)(cntfrq / 1000000), number, 0);
        boot_log_line_append(line, number);
        boot_log_line_append(line, " MHz");
        boot_log_add(line);
    }
    boot_log_add_memory_info(boot_info);
    if (boot_info) {
        boot_log_add_dec("Boot modules: ", boot_info->module_count, " loaded");
    }
}
static void k_font_init(void *data) {
    if (!data)
        return;
    uint32_t *ptr = (uint32_t *)data;
    k_boot_font.magic = ptr[0];
    k_boot_font.tex_width = ptr[1];
    k_boot_font.tex_height = ptr[2];
    k_boot_font.line_height = ptr[3];
    k_boot_font.ascent = ptr[4];
    k_boot_font.count = ptr[5];
    k_boot_font.glyphs = (K_GlyphInfo *)(ptr + 6);
    k_boot_font.bitmap =
        (uint32_t *)((uint8_t *)k_boot_font.glyphs + k_boot_font.count * sizeof(K_GlyphInfo));
}
static K_GlyphInfo *k_glyph_find(uint32_t cp) {
    int lo = 0, hi = (int)k_boot_font.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mcp = k_boot_font.glyphs[mid].codepoint;
        if (mcp == cp)
            return &k_boot_font.glyphs[mid];
        if (mcp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}
static int k_draw_string_atlas(int x, int y, const char *str, uint32_t color) {
    if (!k_font_virt_addr)
        return x;
    while (*str) {
        uint8_t b = (uint8_t)*str;
        uint32_t cp;
        int n = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            cp = ((uint32_t)(b & 0x1F) << 6) | ((uint8_t)str[1] & 0x3F);
            n = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)((uint8_t)str[1] & 0x3F) << 6) |
                 ((uint8_t)str[2] & 0x3F);
            n = 3;
        } else {
            cp = '?';
            n = 1;
        }
        str += n;
        K_GlyphInfo *g = k_glyph_find(cp);
        if (!g) {
            x += k_boot_font.line_height / 2;
            continue;
        }
        if (g->w == 0) {
            x += g->advance;
            continue;
        }
        for (int iy = 0; iy < (int)g->h; iy++) {
            for (int ix = 0; ix < (int)g->w; ix++) {
                uint32_t px = k_boot_font.bitmap[(g->y + iy) * k_boot_font.tex_width + (g->x + ix)];
                uint8_t a = (px >> 24) & 0xFF;
                if (a > 0)
                    put_pixel_AA(x + g->off_x + ix, y + g->off_y + iy, color, a);
            }
        }
        x += g->advance;
    }
    return x;
}
static void k_draw_boot_line(int x, int y, const char *str) {
    const char *bracket_end = str;
    while (*bracket_end && *bracket_end != ']')
        bracket_end++;
    uint32_t text_color = 0x888888;
    if (strstr(str, "NOTICE"))
        text_color = COLOR_WHITE;
    else if (strstr(str, "INFO"))
        text_color = 0x44CC44;
    if (*bracket_end == ']') {
        char prefix[32];
        int len = (bracket_end - str) + 1;
        if (len > 31)
            len = 31;
        strncpy(prefix, str, len);
        prefix[len] = 0;
        int next_x = k_draw_string_atlas(x, y, prefix, 0x555555);
        k_draw_string_atlas(next_x, y, bracket_end + 1, text_color);
    } else {
        k_draw_string_atlas(x, y, str, text_color);
    }
}
void draw_boot_splash() {
    if (k_logo_phys_addr == 0 || !k_font_virt_addr)
        return;
    void *logo_data = map_module(k_logo_phys_addr, k_logo_size);
    k_font_init(k_font_virt_addr);
    BITMAPINFOHEADER *info = (BITMAPINFOHEADER *)(logo_data + sizeof(BITMAPFILEHEADER));
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
        int line_h = k_boot_font.line_height - 2;
        for (int m = 0; m <= current_line_limit && m < (int)boot_log_count; m++) {
            k_draw_boot_line(12, 8 + m * line_h, boot_log_entries[m]);
        }
        drv_fb_swap_buffers();
        int delay_multiplier = 1;
        if (current_line_limit != last_line) {
            if (current_line_limit == 4 || current_line_limit == 12 || current_line_limit == 18 ||
                current_line_limit == 26 || current_line_limit == 30)
                delay_multiplier = 15;
            last_line = current_line_limit;
        }
        for (volatile int d = 0; d < 200000 * delay_multiplier; d++)
            asm volatile("nop" ::: "memory");
    }
}
