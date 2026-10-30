#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table are filled
 * by kernel-entrypoint.s during the paging bootstrap (identity map of the
 * first 4 MiB, higher-half map at PDE[0x300] and recursive PDE[1023]).
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;
__attribute__((aligned(0x1000))) struct PageTable     _paging_kernel_page_table;

/**
 * Global page frame manager state tracking free/busy frames.
 * The first KERNEL_RESERVED_PAGE_FRAME_COUNT frames (first 4 MiB) are
 * reserved for the kernel, GRUB and memory-mapped I/O.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

/* Page directories handed out for processes (Chapter 4) */
__attribute__((aligned(0x1000))) static struct PageDirectory
    page_directory_list[PAGING_DIRECTORY_TABLE_MAX_COUNT] = {0};

static struct {
    bool page_directory_used[PAGING_DIRECTORY_TABLE_MAX_COUNT];
} page_directory_manager = {
    .page_directory_used = {false},
};

/**
 * The 32-bit PDE/PTE layout splits the 20-bit page frame number across
 * several bitfields (bits 12..31), so a raw 4-byte write is the only
 * reliable way to encode/decode the address. memcpy keeps the compiler
 * from assuming strict aliasing between the bitfield struct and a uint32_t.
 */
static void paging_write_entry(void *entry, uint32_t frame_number, uint8_t flags) {
    uint32_t word = ((frame_number & 0xFFFFFu) << 12) | (flags & 0xFFu);
    memcpy(entry, &word, sizeof(word));
}

static uint32_t paging_read_entry_frame(const void *entry) {
    uint32_t word;
    memcpy(&word, entry, sizeof(word));
    return word >> 12;
}

static uint8_t paging_flag_byte(struct PageDirectoryEntryFlag flag) {
    uint8_t byte;
    memcpy(&byte, &flag, sizeof(byte));
    return byte;
}

static struct PageDirectoryEntryFlag paging_user_flag(void) {
    struct PageDirectoryEntryFlag flag = {
        .present_bit       = true,
        .write_bit         = true,
        .us_bit            = true,
        .pwt_bit           = false,
        .pcd_bit           = false,
        .accessed_bit      = false,
        .dirty_bit         = false,
        .use_pagesize_4_mb = false,
    };
    return flag;
}

/**
 * Set a page table entry for the given virtual address. Internal version,
 * returns false when no page table could be created for the PDE.
 */
static bool update_page_directory_entry_internal(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageDirectoryEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;

    paging_write_entry(&page_table->table[page_table_index],
                       (uint32_t) physical_addr >> 12,
                       paging_flag_byte(flag));
    flush_single_tlb(virtual_addr);
    return true;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageDirectoryEntryFlag flag
) {
    update_page_directory_entry_internal(page_dir, physical_addr, virtual_addr, flag);
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_directory_index >= PAGE_ENTRY_COUNT ||
        page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX ||
        page_dir != paging_get_current_page_directory_addr()) {
        return NULL;
    }

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];

    if (dir_entry->flag.present_bit) {
        return (struct PageTable *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS
                                     + page_directory_index * PAGE_FRAME_SIZE);
    }

    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;

            struct PageDirectoryEntryFlag flag = paging_user_flag();
            paging_write_entry(dir_entry, page_frame_number, paging_flag_byte(flag));

            struct PageTable *page_table = (struct PageTable *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS
                                                                  + page_directory_index * PAGE_FRAME_SIZE);
            flush_single_tlb(page_table);
            memset(page_table, 0, PAGE_FRAME_SIZE);
            return page_table;
        }
    }
    return NULL;
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "r"(virtual_addr) : "memory");
}

/* --- Memory Management --- */

bool paging_allocate_check(uint32_t amount) {
    uint32_t required_frames = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    return page_manager_state.free_page_frame_count >= required_frames;
}

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_frame_number = PAGE_FRAME_MAX_COUNT;
    for (uint32_t i = KERNEL_RESERVED_PAGE_FRAME_COUNT; i < PAGE_FRAME_MAX_COUNT; i++) {
        if (!page_manager_state.page_frame_map[i]) {
            page_frame_number = i;
            break;
        }
    }
    if (page_frame_number == PAGE_FRAME_MAX_COUNT) return false;

    page_manager_state.page_frame_map[page_frame_number] = true;
    page_manager_state.free_page_frame_count--;

    struct PageDirectoryEntryFlag flag = paging_user_flag();
    bool mapped = update_page_directory_entry_internal(
        page_dir,
        (void *) (page_frame_number * PAGE_FRAME_SIZE),
        virtual_addr,
        flag
    );

    if (!mapped) {
        page_manager_state.page_frame_map[page_frame_number] = false;
        page_manager_state.free_page_frame_count++;
        return false;
    }

    /* User frames are never assumed to contain zeroes after allocation. */
    memset(virtual_addr, 0, PAGE_FRAME_SIZE);
    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;

    if (page_dir == NULL) return false;
    if (!page_dir->table[page_directory_index].flag.present_bit) return false;
    if (page_dir->table[page_directory_index].flag.use_pagesize_4_mb) return false;

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;

    struct PageTableEntry *entry = &page_table->table[page_table_index];
    if (!entry->flag.present_bit) return false;

    uint32_t page_frame_number = paging_read_entry_frame(entry);
    if (page_frame_number < KERNEL_RESERVED_PAGE_FRAME_COUNT ||
        page_frame_number >= PAGE_FRAME_MAX_COUNT) {
        return false;
    }

    if (page_manager_state.page_frame_map[page_frame_number]) {
        page_manager_state.page_frame_map[page_frame_number] = false;
        page_manager_state.free_page_frame_count++;
    }

    uint32_t zero = 0;
    memcpy(entry, &zero, sizeof(zero));
    flush_single_tlb(virtual_addr);
    return true;
}

struct PageDirectory *paging_create_new_page_directory(void) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (!page_directory_manager.page_directory_used[i]) {
            page_directory_manager.page_directory_used[i] = true;
            memset(&page_directory_list[i], 0, sizeof(struct PageDirectory));

            /* Share the kernel higher-half mapping. */
            page_directory_list[i].table[0x300] = _paging_kernel_page_directory.table[0x300];

            /* The fractal entry must point to this very page directory so
             * that paging_get_page_table() can reach this process' page
             * tables through RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS. */
            uint32_t page_directory_physical_addr =
                (uint32_t) &page_directory_list[i] - KERNEL_VIRTUAL_ADDRESS_BASE;
            paging_write_entry(
                &page_directory_list[i].table[RECURSIVE_PAGE_DIRECTORY_INDEX],
                page_directory_physical_addr >> 12,
                0x3
            );
            return &page_directory_list[i];
        }
    }
    return NULL;
}

bool paging_free_page_directory(struct PageDirectory *page_dir) {
    if (page_dir == NULL) return false;

    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (&page_directory_list[i] != page_dir) continue;
        if (!page_directory_manager.page_directory_used[i]) return false;

        for (uint32_t pde = 0; pde < PAGE_ENTRY_COUNT; pde++) {
            if (pde == 0x300 || pde == RECURSIVE_PAGE_DIRECTORY_INDEX) continue;
            if (!page_dir->table[pde].flag.present_bit) continue;
            if (page_dir->table[pde].flag.use_pagesize_4_mb) continue;

            uint32_t page_table_frame = paging_read_entry_frame(&page_dir->table[pde]);
            if (page_table_frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT &&
                page_table_frame < PAGE_FRAME_MAX_COUNT &&
                page_manager_state.page_frame_map[page_table_frame]) {
                page_manager_state.page_frame_map[page_table_frame] = false;
                page_manager_state.free_page_frame_count++;
            }
        }

        memset(page_dir, 0, sizeof(struct PageDirectory));
        page_directory_manager.page_directory_used[i] = false;
        return true;
    }
    return false;
}

struct PageDirectory *paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr) : /* <Empty> */);
    uint32_t virtual_addr_page_dir = current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE;
    return (struct PageDirectory *) virtual_addr_page_dir;
}

void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr) {
    uint32_t physical_addr_page_dir = (uint32_t) page_dir_virtual_addr;
    if ((uint32_t) page_dir_virtual_addr >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        physical_addr_page_dir -= KERNEL_VIRTUAL_ADDRESS_BASE;
    }
    __asm__ volatile("mov %0, %%cr3" : /* <Empty> */ : "r"(physical_addr_page_dir) : "memory");
}
