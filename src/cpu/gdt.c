#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // Null descriptor
        {
            .segment_low  = 0,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0,
            .non_system   = 0,
            .dpl          = 0,
            .present      = 0,
            .segment_high = 0,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 0,
            .granularity  = 0,
            .base_high    = 0
        },
        // Kernel code segment
        {
            .segment_low  = 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0xA,    // Code, not accessed, readable, not conforming
            .non_system   = 1,      // Code or Data Segment
            .dpl          = 0,      // PL0 / Kernel
            .present      = 1,
            .segment_high = 0xF,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 1,      // 32-bit operand
            .granularity  = 1,      // 4 KB
            .base_high    = 0
        },
        // Kernel data segment
        {
            .segment_low  = 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0x2,    // Data, not accessed, writable, direction up
            .non_system   = 1,
            .dpl          = 0,
            .present      = 1,
            .segment_high = 0xF,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 1,
            .granularity  = 1,
            .base_high    = 0
        },
        // User code segment (DPL 3)
        {
            .segment_low  = 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0xA,    // Code, not accessed, readable, not conforming
            .non_system   = 1,
            .dpl          = 3,      // PL3 / User
            .present      = 1,
            .segment_high = 0xF,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 1,
            .granularity  = 1,
            .base_high    = 0
        },
        // User data segment (DPL 3)
        {
            .segment_low  = 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0x2,    // Data, not accessed, writable, direction up
            .non_system   = 1,
            .dpl          = 3,
            .present      = 1,
            .segment_high = 0xF,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 1,
            .granularity  = 1,
            .base_high    = 0
        },
        // TSS descriptor (system segment, base patched by gdt_install_tss)
        {
            .segment_low  = sizeof(struct TSSEntry) & 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0x9,    // 32-bit TSS (available)
            .non_system   = 0,      // System segment
            .dpl          = 0,
            .present      = 1,
            .segment_high = (sizeof(struct TSSEntry) >> 16) & 0xF,
            .avl          = 0,
            .long_mode    = 0,
            .default_op   = 1,      // 32-bit
            .granularity  = 0,      // byte granularity
            .base_high    = 0
        }
    }
};

struct GDTR _gdt_gdtr = {
    .size    = sizeof(struct GlobalDescriptorTable) - 1,
    .address = &global_descriptor_table
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_low  = (uint16_t) (base & 0xFFFF);
    global_descriptor_table.table[5].base_mid  = (uint8_t) ((base >> 16) & 0xFF);
    global_descriptor_table.table[5].base_high = (uint8_t) ((base >> 24) & 0xFF);
}
