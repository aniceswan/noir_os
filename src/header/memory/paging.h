#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYSTEM_MEMORY_MB
 */
#define SYSTEM_MEMORY_MB     128

// Kernel higher-half virtual address base (virtual = physical + this)
#define KERNEL_VIRTUAL_ADDRESS_BASE 0xC0000000u

// Number of entries in a page directory / page table
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

// First 4 MiB of physical memory are reserved for kernel / GRUB / MMIO
#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)

// Recursive (fractal) mapping of the page directory itself
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

// Maximum number of page directories (one per process)
#define PAGING_DIRECTORY_TABLE_MAX_COUNT 32

// kernel's page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable     _paging_kernel_page_table;

/**
 * Page Directory Entry Flag, only first 8 bit.
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 *
 * @param present_bit        Entry exists / valid
 * @param write_bit          Page is writable
 * @param us_bit             User/supervisor (1 = accessible from ring 3)
 * @param pwt_bit            Page-level write-through
 * @param pcd_bit            Page-level cache disable
 * @param accessed_bit       Set by MMU when accessed
 * @param dirty_bit          Ignored on PDE, dirty on PTE (set by MMU)
 * @param use_pagesize_4_mb  PS bit; must stay 0 because we use 4 KiB pages
 */
struct PageDirectoryEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t pwt_bit            : 1;
    uint32_t pcd_bit            : 1;
    uint32_t accessed_bit       : 1;
    uint32_t dirty_bit          : 1;
    uint32_t use_pagesize_4_mb  : 1;
} __attribute__((packed));

/**
 * Page Table Entry Flag, only first 8 bit.
 * Layout matches PageDirectoryEntryFlag; bit 7 is PAT for 4 KiB pages.
 */
struct PageTableEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t pwt_bit            : 1;
    uint32_t pcd_bit            : 1;
    uint32_t accessed_bit       : 1;
    uint32_t dirty_bit          : 1;
    uint32_t use_pagesize_4_mb  : 1;
} __attribute__((packed));

/**
 * Page Directory Entry: level-1 entry pointing to a PageTable.
 * 32-bit non-PAE layout. Bits 12..31 hold the page table frame number.
 * Note: these bitfields must match the Intel Manual exactly; a wrong
 * layout will cause a triple fault.
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;   // bits 0..7
    uint16_t global_page      : 1;        // bit 8
    uint16_t ignored          : 3;        // bits 9..11
    uint16_t page_attr_table  : 1;        // bit 12 (address bit for 4 KiB pages)
    uint16_t higher_address   : 8;        // bits 13..20 (address bits 13..20)
    uint16_t reserved         : 1;        // bit 21 (address bit 21)
    uint16_t page_table_base_address : 10; // bits 22..31 (address bits 22..31)
} __attribute__((packed));

/**
 * Page Table Entry: level-2 entry pointing to a 4 KiB physical frame.
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;       // bits 0..7
    uint16_t global_page      : 1;        // bit 8
    uint16_t ignored          : 3;        // bits 9..11
    uint16_t page_attr_table  : 1;        // bit 12 (address bit for 4 KiB pages)
    uint16_t higher_address   : 8;        // bits 13..20 (address bits 13..20)
    uint16_t reserved         : 1;        // bit 21 (address bit 21)
    uint16_t physical_page_base_address : 10; // bits 22..31 (address bits 22..31)
} __attribute__((packed));

/**
 * Page Directory, containing an array of PageDirectoryEntry.
 * Must be 4 KiB aligned (Intel Manual), otherwise triple fault.
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page Table, containing an array of PageTableEntry.
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page manager state.
 *
 * @param page_frame_map      true when the 4 KiB page frame is currently used
 * @param free_page_frame_count number of free managed page frames
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

/**
 * Edit page directory with respective parameter
 *
 * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageDirectoryEntryFlag flag
);

/**
 * Get the requested page table from respective parameter.
 * Uses fractal (recursive) page mapping, so PDE[1023] must map the
 * page directory itself.
 *
 * @param page_dir      Page directory to read (must be the active one)
 * @param page_directory_index Page table's position/index on page directory
 */
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

/**
 * Invalidate the page containing the virtual address in parameter
 *
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

/* --- Memory Management --- */

/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate a single user page frame in page directory
 *
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             true if a frame was allocated and mapped
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate a single user page frame in page directory
 *
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be freed
 * @return              true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/* --- Process-related Memory Management (used from Chapter 4 onwards) --- */

/**
 * Create a new page directory: an empty user space plus the shared kernel
 * higher-half mapping.
 *
 * @return New page directory virtual address, or NULL when exhausted
 */
struct PageDirectory *paging_create_new_page_directory(void);

/**
 * Free a page directory created by paging_create_new_page_directory().
 *
 * @param page_dir Page directory virtual address
 * @return true when the page directory belongs to the manager
 */
bool paging_free_page_directory(struct PageDirectory *page_dir);

/**
 * Get the currently active (CR3) page directory virtual address
 */
struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Load page directory into CR3
 *
 * @param page_dir_virtual_addr Page directory virtual address
 */
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr);

#endif
