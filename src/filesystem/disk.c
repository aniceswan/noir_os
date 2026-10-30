#include "header/filesystem/disk.h"
#include "header/cpu/portio.h"

/**
 * Bounded busy wait so a missing/unresponsive drive does not hang the kernel.
 * QEMU's ATA PIO completes within a handful of port reads.
 */
#define ATA_WAIT_TIMEOUT 1000000u

static void ATA_busy_wait(void) {
    uint32_t timeout = ATA_WAIT_TIMEOUT;
    while ((in(0x1F7) & ATA_STATUS_BSY) && timeout--);
}

static void ATA_DRQ_wait(void) {
    uint32_t timeout = ATA_WAIT_TIMEOUT;
    while (!(in(0x1F7) & ATA_STATUS_RDY) && timeout--);
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((logical_block_address >> 24) & 0xF));
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) logical_block_address);
    out(0x1F4, (uint8_t) (logical_block_address >> 8));
    out(0x1F5, (uint8_t) (logical_block_address >> 16));
    out(0x1F7, 0x20);

    uint16_t *target = (uint16_t*) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            target[j] = in16(0x1F0);
        target += HALF_BLOCK_SIZE;
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((logical_block_address >> 24) & 0xF));
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) logical_block_address);
    out(0x1F4, (uint8_t) (logical_block_address >> 8));
    out(0x1F5, (uint8_t) (logical_block_address >> 16));
    out(0x1F7, 0x30);

    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            out16(0x1F0, ((const uint16_t*) ptr)[HALF_BLOCK_SIZE*i + j]);
    }
}
