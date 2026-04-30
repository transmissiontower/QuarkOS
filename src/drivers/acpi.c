// SPDX-License-Identifier: MIT
/*
 * src/drivers/acpi.c - ACPI table parsing, power management, shutdown and reboot
 */
#include "drivers/acpi.h"
#include "mm/memory.h"
#include "util/util.h"
#define RSDP_SIGNATURE "RSD PTR "
#define SCI_EN_FLAG 0x0001
#define SLP_EN_FLAG 0x2000
#define SLP_TYP_SHIFT 10
#define LEGACY_SHUTDOWN_PORT 0x604
static struct rsdp_descriptor *acpi_rsdp;
static struct acpi_header *acpi_rsdt;
static struct fadt_header *acpi_fadt;
static uint16_t pm1a_cnt_block;
static uint16_t pm1b_cnt_block;
static uint8_t s5_slp_typ_a;
static uint8_t s5_slp_typ_b;
static struct acpi_generic_address reset_reg;
static uint8_t reset_value;
static bool acpi_initialized = false;
static bool power_capable = false;
static bool reset_capable = false;
static uint8_t acpi_calculate_checksum(uint8_t *ptr, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    return sum;
}
static struct rsdp_descriptor *acpi_scan_rsdp(uint32_t start, uint32_t end) {
    for (uint8_t *ptr = (uint8_t *)start; ptr < (uint8_t *)end; ptr += 16) {
        if (memcmp(ptr, RSDP_SIGNATURE, 8) == 0) {
            struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)ptr;
            if (acpi_calculate_checksum((uint8_t *)rsdp, sizeof(struct rsdp_descriptor)) == 0) {
                return rsdp;
            }
        }
    }
    return 0;
}
static struct rsdp_descriptor *acpi_find_rsdp(void) {
    return acpi_scan_rsdp(0xE0000, 0x100000);
}
static struct acpi_header *acpi_map_full_table(uint32_t phys_addr) {
    struct acpi_header *hdr =
        (struct acpi_header *)map_module(phys_addr, sizeof(struct acpi_header));
    if (!hdr)
        return 0;
    return (struct acpi_header *)map_module(phys_addr, hdr->length);
}
static bool acpi_validate_table(struct acpi_header *hdr) {
    if (!hdr)
        return false;
    return acpi_calculate_checksum((uint8_t *)hdr, hdr->length) == 0;
}
static uint32_t acpi_parse_pkg_length(uint8_t **ptr, uint8_t *end) {
    if (*ptr >= end)
        return 0;
    uint8_t first = **ptr;
    (*ptr)++;
    uint32_t length = first & 0x3F;
    uint8_t bytes_follow = (first >> 6) & 0x3;
    for (uint8_t i = 0; i < bytes_follow && *ptr < end; i++) {
        length |= ((uint32_t)(**ptr)) << (8 * (i + 1));
        (*ptr)++;
    }
    return length;
}
static uint32_t acpi_parse_pkg_value(uint8_t **ptr, uint8_t *end) {
    if (*ptr >= end)
        return 0;
    uint8_t prefix = **ptr;
    (*ptr)++;
    switch (prefix) {
    case 0x0A: {
        if (*ptr >= end)
            return 0;
        uint32_t val = **ptr;
        (*ptr)++;
        return val;
    }
    case 0x0B: {
        if (*ptr + 1 >= end)
            return 0;
        uint32_t val = (*ptr)[0] | ((*ptr)[1] << 8);
        (*ptr) += 2;
        return val;
    }
    case 0x0C: {
        if (*ptr + 3 >= end)
            return 0;
        uint32_t val = (*ptr)[0] | ((*ptr)[1] << 8) | ((*ptr)[2] << 16) | ((*ptr)[3] << 24);
        (*ptr) += 4;
        return val;
    }
    default:
        return prefix;
    }
}
static void acpi_extract_s5(void) {
    if (!acpi_fadt || !acpi_fadt->dsdt)
        return;
    struct acpi_header *dsdt_hdr = acpi_map_full_table(acpi_fadt->dsdt);
    if (!dsdt_hdr)
        return;
    uint8_t *start = (uint8_t *)dsdt_hdr;
    uint8_t *end = start + dsdt_hdr->length;
    uint8_t *ptr = start + sizeof(struct acpi_header);
    while (ptr < end - 4) {
        if (memcmp(ptr, "_S5_", 4) == 0) {
            uint8_t *pkg = ptr + 4;
            while (pkg < end && (*pkg == 0x20 || *pkg == '\t'))
                pkg++;
            if (pkg >= end || *pkg != 0x12) {
                ptr += 4;
                continue;
            }
            pkg++;
            acpi_parse_pkg_length(&pkg, end);
            if (pkg >= end)
                break;
            uint8_t elements = *pkg++;
            if (elements < 2)
                break;
            uint32_t val_a = acpi_parse_pkg_value(&pkg, end);
            uint32_t val_b = acpi_parse_pkg_value(&pkg, end);
            s5_slp_typ_a = (uint8_t)val_a;
            s5_slp_typ_b = (uint8_t)val_b;
            power_capable = true;
            return;
        }
        ptr++;
    }
}
static void acpi_enable(void) {
    if (!acpi_fadt)
        return;
    if (!(pm1a_cnt_block && (inw(pm1a_cnt_block) & SCI_EN_FLAG))) {
        if (acpi_fadt->SMI_CMD && acpi_fadt->acpi_enable) {
            outb(acpi_fadt->SMI_CMD, acpi_fadt->acpi_enable);
            for (int i = 0; i < 100000; i++) {
                if (pm1a_cnt_block && (inw(pm1a_cnt_block) & SCI_EN_FLAG))
                    break;
            }
        }
    }
}
int acpi_init(void) {
    acpi_rsdp = acpi_find_rsdp();
    if (!acpi_rsdp)
        return 0;
    acpi_rsdt = acpi_map_full_table(acpi_rsdp->rsdt_address);
    if (!acpi_validate_table(acpi_rsdt))
        return 0;
    acpi_fadt = (struct fadt_header *)acpi_find_table("FACP");
    if (!acpi_fadt || !acpi_validate_table(&acpi_fadt->header))
        return 0;
    pm1a_cnt_block = (uint16_t)acpi_fadt->PM1a_CNT_BLK;
    pm1b_cnt_block = (uint16_t)acpi_fadt->PM1b_CNT_BLK;
    reset_reg = acpi_fadt->reset_reg;
    reset_value = acpi_fadt->reset_value;
    reset_capable = (reset_reg.address != 0);
    acpi_extract_s5();
    acpi_initialized = true;
    return acpi_initialized;
}
bool acpi_power_ready(void) {
    return acpi_initialized && power_capable && pm1a_cnt_block;
}
bool acpi_power_button_available(void) {
    return acpi_initialized && power_capable;
}
bool acpi_reset_available(void) {
    return acpi_initialized && reset_capable;
}
void *acpi_find_table(const char *sig) {
    if (!acpi_rsdt)
        return 0;
    uint32_t entries = (acpi_rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t *table_ptrs = (uint32_t *)((uint8_t *)acpi_rsdt + sizeof(struct acpi_header));
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t phys = table_ptrs[i];
        if (!phys)
            continue;
        struct acpi_header *hdr =
            (struct acpi_header *)map_module(phys, sizeof(struct acpi_header));
        if (!hdr)
            continue;
        if (memcmp(hdr->signature, sig, 4) == 0) {
            return map_module(phys, hdr->length);
        }
    }
    return 0;
}
int acpi_get_cpu_count(void) {
    struct madt_header *madt = (struct madt_header *)acpi_find_table("APIC");
    if (!madt)
        return 1;
    uint8_t *ptr = (uint8_t *)(madt + 1);
    uint8_t *end = ((uint8_t *)madt) + madt->header.length;
    int count = 0;
    while (ptr + sizeof(struct madt_entry_header) <= end) {
        struct madt_entry_header *hdr = (struct madt_entry_header *)ptr;
        if (hdr->length == 0)
            break;
        if (hdr->type == 0 && hdr->length >= 8) {
            if (ptr[4] & 0x01)
                count++;
        }
        ptr += hdr->length;
    }
    return count > 0 ? count : 1;
}
void acpi_shutdown(void) {
    asm volatile("cli");
    if (acpi_power_ready()) {
        acpi_enable();
        uint16_t value = (uint16_t)((s5_slp_typ_a << SLP_TYP_SHIFT) | SLP_EN_FLAG);
        outw(pm1a_cnt_block, value);
        if (pm1b_cnt_block) {
            uint16_t value_b = (uint16_t)((s5_slp_typ_b << SLP_TYP_SHIFT) | SLP_EN_FLAG);
            outw(pm1b_cnt_block, value_b);
        }
    }
    outw(LEGACY_SHUTDOWN_PORT, 0x2000);
    for (;;) {
        outb(0x64, 0xFE);
        asm volatile("hlt");
    }
}
void acpi_reboot(void) {
    asm volatile("cli");
    if (reset_capable && reset_reg.address) {
        if (reset_reg.address_space == 1) {
            outb((uint16_t)reset_reg.address, reset_value);
            for (;;)
                asm volatile("hlt");
        } else if (reset_reg.address_space == 0) {
            uint8_t *reg = (uint8_t *)map_module((uint32_t)reset_reg.address, 1);
            if (reg) {
                *reg = reset_value;
                for (;;)
                    asm volatile("hlt");
            }
        }
    }
    outb(0x64, 0xFE);
    for (;;)
        asm volatile("hlt");
}
