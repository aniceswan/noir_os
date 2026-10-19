#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

/* =================== HELPER & REGULAR FUNCTIONS ============================ */

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

char *get_entry_name(void *entry) {
    return (char*) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry*) ((uint8_t*) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    if (entry == NULL || entry->rec_len == 0) return NULL;
    return (struct EXT2DirectoryEntry*) ((uint8_t*) entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    uint16_t len = (uint16_t) sizeof(struct EXT2DirectoryEntry) + name_len;
    if (len % 4 != 0) {
        len += (uint16_t) (4 - (len % 4));
    }
    return len;
}

uint32_t get_dir_first_child_offset(void *ptr) {
    (void) ptr;
    return 24;
}

static void read_inode(uint32_t inode_num, struct EXT2Inode *node) {
    if (inode_num < 1 || inode_num > GROUPS_COUNT * INODES_PER_GROUP) return;
    uint32_t bgd = inode_to_bgd(inode_num);
    uint32_t local = inode_to_local(inode_num);
    uint32_t block_in_table = local / INODES_PER_TABLE;
    uint32_t offset_in_block = (local % INODES_PER_TABLE) * INODE_SIZE;
    uint32_t table_lba = bgd_table.table[bgd].bg_inode_table + block_in_table;

    struct BlockBuffer buf;
    read_blocks(&buf, table_lba, 1);
    memcpy(node, buf.buf + offset_in_block, sizeof(struct EXT2Inode));
}

void sync_node(struct EXT2Inode *node, uint32_t inode_num) {
    if (inode_num < 1 || inode_num > GROUPS_COUNT * INODES_PER_GROUP) return;
    uint32_t bgd = inode_to_bgd(inode_num);
    uint32_t local = inode_to_local(inode_num);
    uint32_t block_in_table = local / INODES_PER_TABLE;
    uint32_t offset_in_block = (local % INODES_PER_TABLE) * INODE_SIZE;
    uint32_t table_lba = bgd_table.table[bgd].bg_inode_table + block_in_table;

    struct BlockBuffer buf;
    read_blocks(&buf, table_lba, 1);
    memcpy(buf.buf + offset_in_block, node, sizeof(struct EXT2Inode));
    write_blocks(&buf, table_lba, 1);
}

static bool is_inode_allocated(uint32_t inode_num) {
    if (inode_num < 1 || inode_num > GROUPS_COUNT * INODES_PER_GROUP) return false;
    uint32_t bgd = inode_to_bgd(inode_num);
    uint32_t local = inode_to_local(inode_num);
    uint32_t bitmap_lba = bgd_table.table[bgd].bg_inode_bitmap;

    struct BlockBuffer buf;
    read_blocks(&buf, bitmap_lba, 1);
    return (buf.buf[local / 8] & (1 << (local % 8))) != 0;
}

uint32_t allocate_node(void) {
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        if (bgd_table.table[g].bg_free_inodes_count > 0) {
            uint32_t bitmap_lba = bgd_table.table[g].bg_inode_bitmap;
            struct BlockBuffer buf;
            read_blocks(&buf, bitmap_lba, 1);
            for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
                if ((buf.buf[i / 8] & (1 << (i % 8))) == 0) {
                    buf.buf[i / 8] |= (1 << (i % 8));
                    write_blocks(&buf, bitmap_lba, 1);

                    bgd_table.table[g].bg_free_inodes_count--;
                    write_blocks(&bgd_table, 2, 1);

                    superblock.s_free_inodes_count--;
                    write_blocks(&superblock, 1, 1);

                    return g * INODES_PER_GROUP + i + 1;
                }
            }
        }
    }
    return 0;
}

void deallocate_node(uint32_t inode_num) {
    if (inode_num < 1 || inode_num > GROUPS_COUNT * INODES_PER_GROUP) return;
    uint32_t g = inode_to_bgd(inode_num);
    uint32_t local = inode_to_local(inode_num);
    uint32_t bitmap_lba = bgd_table.table[g].bg_inode_bitmap;

    struct BlockBuffer buf;
    read_blocks(&buf, bitmap_lba, 1);
    if ((buf.buf[local / 8] & (1 << (local % 8))) != 0) {
        buf.buf[local / 8] &= ~(1 << (local % 8));
        write_blocks(&buf, bitmap_lba, 1);

        bgd_table.table[g].bg_free_inodes_count++;
        write_blocks(&bgd_table, 2, 1);

        superblock.s_free_inodes_count++;
        write_blocks(&superblock, 1, 1);
    }
}

static uint32_t allocate_block(uint32_t prefered_bgd) {
    uint32_t order[GROUPS_COUNT];
    order[0] = prefered_bgd % GROUPS_COUNT;
    uint32_t idx = 1;
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        if (i != order[0]) {
            order[idx++] = i;
        }
    }

    for (uint32_t k = 0; k < GROUPS_COUNT; k++) {
        uint32_t g = order[k];
        if (bgd_table.table[g].bg_free_blocks_count > 0) {
            uint32_t bitmap_lba = bgd_table.table[g].bg_block_bitmap;
            struct BlockBuffer buf;
            read_blocks(&buf, bitmap_lba, 1);
            for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
                if ((buf.buf[i / 8] & (1 << (i % 8))) == 0) {
                    buf.buf[i / 8] |= (1 << (i % 8));
                    write_blocks(&buf, bitmap_lba, 1);

                    bgd_table.table[g].bg_free_blocks_count--;
                    write_blocks(&bgd_table, 2, 1);

                    superblock.s_free_blocks_count--;
                    write_blocks(&superblock, 1, 1);

                    return g * BLOCKS_PER_GROUP + i;
                }
            }
        }
    }
    return 0;
}

static void deallocate_block_single(uint32_t block_id) {
    if (block_id >= DISK_SPACE / BLOCK_SIZE) return;
    uint32_t g = block_id / BLOCKS_PER_GROUP;
    uint32_t local = block_id % BLOCKS_PER_GROUP;
    uint32_t bitmap_lba = bgd_table.table[g].bg_block_bitmap;

    struct BlockBuffer buf;
    read_blocks(&buf, bitmap_lba, 1);
    if ((buf.buf[local / 8] & (1 << (local % 8))) != 0) {
        buf.buf[local / 8] &= ~(1 << (local % 8));
        write_blocks(&buf, bitmap_lba, 1);

        bgd_table.table[g].bg_free_blocks_count++;
        write_blocks(&bgd_table, 2, 1);

        superblock.s_free_blocks_count++;
        write_blocks(&superblock, 1, 1);
    }
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    uint32_t *i_block = (uint32_t*) loc;
    (void) blocks;

    // 1. Direct blocks
    for (uint32_t i = 0; i < 12; i++) {
        if (i_block[i] != 0) {
            deallocate_block_single(i_block[i]);
            i_block[i] = 0;
        }
    }

    // 2. Single indirect block
    if (i_block[12] != 0) {
        struct BlockBuffer indirect_buf;
        read_blocks(&indirect_buf, i_block[12], 1);
        uint32_t *ptrs = (uint32_t*) indirect_buf.buf;
        for (uint32_t j = 0; j < BLOCK_SIZE / 4; j++) {
            if (ptrs[j] != 0) {
                deallocate_block_single(ptrs[j]);
            }
        }
        deallocate_block_single(i_block[12]);
        i_block[12] = 0;
    }

    // 3. Doubly indirect block
    if (i_block[13] != 0) {
        struct BlockBuffer d1_buf;
        read_blocks(&d1_buf, i_block[13], 1);
        uint32_t *d1_ptrs = (uint32_t*) d1_buf.buf;
        for (uint32_t j = 0; j < BLOCK_SIZE / 4; j++) {
            if (d1_ptrs[j] != 0) {
                struct BlockBuffer d2_buf;
                read_blocks(&d2_buf, d1_ptrs[j], 1);
                uint32_t *d2_ptrs = (uint32_t*) d2_buf.buf;
                for (uint32_t k = 0; k < BLOCK_SIZE / 4; k++) {
                    if (d2_ptrs[k] != 0) {
                        deallocate_block_single(d2_ptrs[k]);
                    }
                }
                deallocate_block_single(d1_ptrs[j]);
            }
        }
        deallocate_block_single(i_block[13]);
        i_block[13] = 0;
    }
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded) {
    (void) locations;
    (void) blocks;
    (void) bitmap;
    (void) depth;
    (void) last_bgd;
    (void) bgd_loaded;
    return 0;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    uint32_t blocks_needed = (node->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint8_t *src = (uint8_t*) ptr;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t blk = allocate_block(prefered_bgd);

        struct BlockBuffer buf;
        memset(&buf, 0, BLOCK_SIZE);
        uint32_t bytes_to_copy = node->i_size - (b * BLOCK_SIZE);
        if (bytes_to_copy > BLOCK_SIZE) bytes_to_copy = BLOCK_SIZE;
        if (src != NULL && bytes_to_copy > 0) {
            memcpy(buf.buf, src + (b * BLOCK_SIZE), bytes_to_copy);
        }
        write_blocks(&buf, blk, 1);

        if (b < 12) {
            node->i_block[b] = blk;
        } else if (b < 12 + 128) {
            uint32_t rel_idx = b - 12;
            if (node->i_block[12] == 0) {
                node->i_block[12] = allocate_block(prefered_bgd);
                struct BlockBuffer zero_buf;
                memset(&zero_buf, 0, BLOCK_SIZE);
                write_blocks(&zero_buf, node->i_block[12], 1);
            }
            struct BlockBuffer ind_buf;
            read_blocks(&ind_buf, node->i_block[12], 1);
            uint32_t *ptrs = (uint32_t*) ind_buf.buf;
            ptrs[rel_idx] = blk;
            write_blocks(&ind_buf, node->i_block[12], 1);
        } else {
            uint32_t rel_idx = b - (12 + 128);
            uint32_t idx1 = rel_idx / 128;
            uint32_t idx2 = rel_idx % 128;

            if (node->i_block[13] == 0) {
                node->i_block[13] = allocate_block(prefered_bgd);
                struct BlockBuffer zero_buf;
                memset(&zero_buf, 0, BLOCK_SIZE);
                write_blocks(&zero_buf, node->i_block[13], 1);
            }
            struct BlockBuffer d1_buf;
            read_blocks(&d1_buf, node->i_block[13], 1);
            uint32_t *d1_ptrs = (uint32_t*) d1_buf.buf;

            if (d1_ptrs[idx1] == 0) {
                d1_ptrs[idx1] = allocate_block(prefered_bgd);
                write_blocks(&d1_buf, node->i_block[13], 1);
                struct BlockBuffer zero_buf;
                memset(&zero_buf, 0, BLOCK_SIZE);
                write_blocks(&zero_buf, d1_ptrs[idx1], 1);
            }

            struct BlockBuffer d2_buf;
            read_blocks(&d2_buf, d1_ptrs[idx1], 1);
            uint32_t *d2_ptrs = (uint32_t*) d2_buf.buf;
            d2_ptrs[idx2] = blk;
            write_blocks(&d2_buf, d1_ptrs[idx1], 1);
        }
    }
    node->i_blocks = blocks_needed;
}

static uint32_t get_node_block_id(struct EXT2Inode *node, uint32_t b) {
    if (b < 12) {
        return node->i_block[b];
    } else if (b < 12 + 128) {
        if (node->i_block[12] == 0) return 0;
        struct BlockBuffer ind_buf;
        read_blocks(&ind_buf, node->i_block[12], 1);
        uint32_t *ptrs = (uint32_t*) ind_buf.buf;
        return ptrs[b - 12];
    } else {
        if (node->i_block[13] == 0) return 0;
        uint32_t rel_idx = b - (12 + 128);
        uint32_t idx1 = rel_idx / 128;
        uint32_t idx2 = rel_idx % 128;

        struct BlockBuffer d1_buf;
        read_blocks(&d1_buf, node->i_block[13], 1);
        uint32_t *d1_ptrs = (uint32_t*) d1_buf.buf;
        if (d1_ptrs[idx1] == 0) return 0;

        struct BlockBuffer d2_buf;
        read_blocks(&d2_buf, d1_ptrs[idx1], 1);
        uint32_t *d2_ptrs = (uint32_t*) d2_buf.buf;
        return d2_ptrs[idx2];
    }
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    struct BlockBuffer buf;
    memset(&buf, 0, BLOCK_SIZE);

    // Entry 0: "."
    struct EXT2DirectoryEntry *e0 = (struct EXT2DirectoryEntry*) buf.buf;
    e0->inode     = inode;
    e0->rec_len   = 12;
    e0->name_len  = 1;
    e0->file_type = EXT2_FT_DIR;
    char *n0      = get_entry_name(e0);
    n0[0]         = '.';

    // Entry 1: ".."
    struct EXT2DirectoryEntry *e1 = (struct EXT2DirectoryEntry*) (buf.buf + 12);
    e1->inode     = parent_inode;
    e1->rec_len   = 12;
    e1->name_len  = 2;
    e1->file_type = EXT2_FT_DIR;
    char *n1      = get_entry_name(e1);
    n1[0]         = '.';
    n1[1]         = '.';

    write_blocks(&buf, node->i_block[0], 1);
}

bool is_directory_empty(uint32_t inode_num) {
    if (inode_num < 1 || inode_num > GROUPS_COUNT * INODES_PER_GROUP) return true;
    struct EXT2Inode node;
    read_inode(inode_num, &node);
    if ((node.i_mode & EXT2_S_IFDIR) == 0) return true;

    uint32_t total_blocks = (node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (total_blocks == 0 && node.i_block[0] != 0) total_blocks = 1;

    for (uint32_t b = 0; b < total_blocks; b++) {
        uint32_t blk = get_node_block_id(&node, b);
        if (blk == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, blk, 1);

        uint32_t offset = (b == 0) ? 24 : 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE) break;
            if (entry->inode != 0) {
                return false;
            }
            offset += entry->rec_len;
        }
    }
    return true;
}

/* =================== INITIALIZER ============================ */

bool is_empty_storage(void) {
    struct BlockBuffer buf;
    read_blocks(&buf, BOOT_SECTOR, 1);
    return (memcmp(buf.buf, fs_signature, BLOCK_SIZE) != 0);
}

void create_ext2(void) {
    // 1. Write fs_signature to Block 0
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    // 2. Setup Superblock
    memset(&superblock, 0, sizeof(struct EXT2Superblock));
    superblock.s_inodes_count      = GROUPS_COUNT * INODES_PER_GROUP; // 512
    superblock.s_blocks_count      = DISK_SPACE / BLOCK_SIZE;         // 8192
    superblock.s_r_blocks_count    = 0;
    superblock.s_free_blocks_count = (1024 - 22) + (7 * (1024 - 18)); // 1002 + 7042 = 8044
    superblock.s_free_inodes_count = (GROUPS_COUNT * INODES_PER_GROUP) - 1; // 511
    superblock.s_first_data_block  = 1;
    superblock.s_first_ino         = 1;
    superblock.s_blocks_per_group  = BLOCKS_PER_GROUP; // 1024
    superblock.s_frags_per_group   = BLOCKS_PER_GROUP; // 1024
    superblock.s_inodes_per_group  = INODES_PER_GROUP; // 64
    superblock.s_magic             = EXT2_SUPER_MAGIC;

    struct BlockBuffer sb_buf;
    memset(&sb_buf, 0, BLOCK_SIZE);
    memcpy(sb_buf.buf, &superblock, sizeof(struct EXT2Superblock));
    write_blocks(&sb_buf, 1, 1);

    // 3. Setup BGD Table
    memset(&bgd_table, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    // Group 0
    bgd_table.table[0].bg_block_bitmap      = 3;
    bgd_table.table[0].bg_inode_bitmap      = 4;
    bgd_table.table[0].bg_inode_table       = 5;
    bgd_table.table[0].bg_free_blocks_count = 1024 - 22; // 1002
    bgd_table.table[0].bg_free_inodes_count = 64 - 1;    // 63
    bgd_table.table[0].bg_used_dirs_count   = 1;         // root dir

    // Groups 1..7
    for (uint32_t g = 1; g < GROUPS_COUNT; g++) {
        bgd_table.table[g].bg_block_bitmap      = g * BLOCKS_PER_GROUP;
        bgd_table.table[g].bg_inode_bitmap      = g * BLOCKS_PER_GROUP + 1;
        bgd_table.table[g].bg_inode_table       = g * BLOCKS_PER_GROUP + 2;
        bgd_table.table[g].bg_free_blocks_count = 1024 - 18; // 1006
        bgd_table.table[g].bg_free_inodes_count = 64;
        bgd_table.table[g].bg_used_dirs_count   = 0;
    }

    struct BlockBuffer bgd_buf;
    memset(&bgd_buf, 0, BLOCK_SIZE);
    memcpy(bgd_buf.buf, &bgd_table, sizeof(struct EXT2BlockGroupDescriptorTable));
    write_blocks(&bgd_buf, 2, 1);

    // 4. Block Bitmaps
    // Group 0: blocks 0..21 are used (22 blocks)
    struct BlockBuffer bmap0;
    memset(&bmap0, 0, BLOCK_SIZE);
    bmap0.buf[0] = 0xFF; // blocks 0..7
    bmap0.buf[1] = 0xFF; // blocks 8..15
    bmap0.buf[2] = 0x3F; // blocks 16..21 (6 bits)
    write_blocks(&bmap0, bgd_table.table[0].bg_block_bitmap, 1);

    // Groups 1..7: blocks 0..17 are used (18 blocks)
    struct BlockBuffer bmap_other;
    memset(&bmap_other, 0, BLOCK_SIZE);
    bmap_other.buf[0] = 0xFF; // blocks 0..7
    bmap_other.buf[1] = 0xFF; // blocks 8..15
    bmap_other.buf[2] = 0x03; // blocks 16..17 (2 bits)
    for (uint32_t g = 1; g < GROUPS_COUNT; g++) {
        write_blocks(&bmap_other, bgd_table.table[g].bg_block_bitmap, 1);
    }

    // 5. Inode Bitmaps
    // Group 0: inode 1 used (bit 0 = 1)
    struct BlockBuffer imap0;
    memset(&imap0, 0, BLOCK_SIZE);
    imap0.buf[0] = 0x01;
    write_blocks(&imap0, bgd_table.table[0].bg_inode_bitmap, 1);

    // Groups 1..7: all 0
    struct BlockBuffer imap_other;
    memset(&imap_other, 0, BLOCK_SIZE);
    for (uint32_t g = 1; g < GROUPS_COUNT; g++) {
        write_blocks(&imap_other, bgd_table.table[g].bg_inode_bitmap, 1);
    }

    // 6. Clear Inode Tables & write Root Inode (Inode 1)
    struct BlockBuffer zero_buf;
    memset(&zero_buf, 0, BLOCK_SIZE);
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        for (uint32_t b = 0; b < INODES_TABLE_BLOCK_COUNT; b++) {
            write_blocks(&zero_buf, bgd_table.table[g].bg_inode_table + b, 1);
        }
    }

    struct EXT2Inode root_inode;
    memset(&root_inode, 0, sizeof(struct EXT2Inode));
    root_inode.i_mode     = EXT2_S_IFDIR;
    root_inode.i_size     = BLOCK_SIZE;
    root_inode.i_blocks   = 1;
    root_inode.i_block[0] = 21; // first data block
    sync_node(&root_inode, 1);

    // 7. Initialize Root Directory Data Block (Block 21)
    init_directory_table(&root_inode, 1, 1);
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    }
    struct BlockBuffer buf;
    read_blocks(&buf, 1, 1);
    memcpy(&superblock, buf.buf, sizeof(struct EXT2Superblock));

    read_blocks(&buf, 2, 1);
    memcpy(&bgd_table, buf.buf, sizeof(struct EXT2BlockGroupDescriptorTable));
}

/* =============================== CRUD FUNCTIONS ======================================== */

int8_t read(struct EXT2DriverRequest request) {
    if (request.name == NULL || request.name_len == 0 || request.buf == NULL) {
        return -1;
    }

    // 1. Validate parent_inode
    if (!is_inode_allocated(request.parent_inode)) {
        return 4; // Parent folder invalid
    }
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 4; // Parent folder invalid
    }

    // 2. Search for entry in parent directory
    uint32_t total_blocks = (parent_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (total_blocks == 0 && parent_node.i_block[0] != 0) total_blocks = 1;

    uint32_t target_inode_num = 0;
    uint8_t target_file_type = 0;

    for (uint32_t b = 0; b < total_blocks; b++) {
        uint32_t blk = get_node_block_id(&parent_node, b);
        if (blk == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, blk, 1);

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE) break;
            if (entry->inode != 0) {
                if (entry->name_len == request.name_len &&
                    memcmp(get_entry_name(entry), request.name, request.name_len) == 0) {
                    target_inode_num = entry->inode;
                    target_file_type = entry->file_type;
                    break;
                }
            }
            offset += entry->rec_len;
        }
        if (target_inode_num != 0) break;
    }

    if (target_inode_num == 0) {
        return 3; // File tidak ditemukan
    }

    // 3. Read target inode & validate
    struct EXT2Inode target_node;
    read_inode(target_inode_num, &target_node);

    if (target_file_type == EXT2_FT_DIR || (target_node.i_mode & EXT2_S_IFDIR) != 0) {
        return 1; // Bukan sebuah file
    }

    if (request.buffer_size < target_node.i_size) {
        return 2; // Buffer tidak cukup
    }

    // 4. Read file content
    uint32_t file_blocks = (target_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint8_t *dst = (uint8_t*) request.buf;

    for (uint32_t b = 0; b < file_blocks; b++) {
        uint32_t blk = get_node_block_id(&target_node, b);
        struct BlockBuffer buf;
        if (blk != 0) {
            read_blocks(&buf, blk, 1);
        } else {
            memset(&buf, 0, BLOCK_SIZE);
        }

        uint32_t bytes_to_copy = target_node.i_size - (b * BLOCK_SIZE);
        if (bytes_to_copy > BLOCK_SIZE) bytes_to_copy = BLOCK_SIZE;
        memcpy(dst + (b * BLOCK_SIZE), buf.buf, bytes_to_copy);
    }

    return 0; // Success
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (prequest == NULL) return -1;

    // 1. Validate parent_inode
    if (!is_inode_allocated(prequest->parent_inode)) {
        return 3; // Parent folder invalid
    }
    struct EXT2Inode parent_node;
    read_inode(prequest->parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3; // Parent folder invalid
    }

    uint32_t target_inode_num = prequest->parent_inode;
    struct EXT2Inode target_node = parent_node;

    // If a specific folder name is provided, search inside parent
    if (prequest->name != NULL && prequest->name_len > 0) {
        uint32_t total_blocks = (parent_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (total_blocks == 0 && parent_node.i_block[0] != 0) total_blocks = 1;

        target_inode_num = 0;
        uint8_t target_file_type = 0;

        for (uint32_t b = 0; b < total_blocks; b++) {
            uint32_t blk = get_node_block_id(&parent_node, b);
            if (blk == 0) continue;

            struct BlockBuffer buf;
            read_blocks(&buf, blk, 1);

            uint32_t offset = 0;
            while (offset < BLOCK_SIZE) {
                struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
                if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE) break;
                if (entry->inode != 0) {
                    if (entry->name_len == prequest->name_len &&
                        memcmp(get_entry_name(entry), prequest->name, prequest->name_len) == 0) {
                        target_inode_num = entry->inode;
                        target_file_type = entry->file_type;
                        break;
                    }
                }
                offset += entry->rec_len;
            }
            if (target_inode_num != 0) break;
        }

        if (target_inode_num == 0) {
            return 2; // Folder tidak ditemukan
        }

        read_inode(target_inode_num, &target_node);
        if (target_file_type == EXT2_FT_REG_FILE || (target_node.i_mode & EXT2_S_IFDIR) == 0) {
            return 1; // Bukan sebuah folder
        }
    }

    // 2. Read directory content into prequest->buf
    if (prequest->buf != NULL) {
        uint32_t dir_blocks = (target_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (dir_blocks == 0 && target_node.i_block[0] != 0) dir_blocks = 1;
        uint8_t *dst = (uint8_t*) prequest->buf;

        for (uint32_t b = 0; b < dir_blocks; b++) {
            uint32_t blk = get_node_block_id(&target_node, b);
            struct BlockBuffer buf;
            if (blk != 0) {
                read_blocks(&buf, blk, 1);
            } else {
                memset(&buf, 0, BLOCK_SIZE);
            }
            uint32_t bytes_to_copy = BLOCK_SIZE;
            if (target_node.i_size > (b * BLOCK_SIZE)) {
                uint32_t rem = target_node.i_size - (b * BLOCK_SIZE);
                if (rem > 0 && rem < BLOCK_SIZE) bytes_to_copy = rem;
            }
            memcpy(dst + (b * BLOCK_SIZE), buf.buf, bytes_to_copy);
        }
    }

    return 0; // Success
}

int8_t write(struct EXT2DriverRequest *request) {
    if (request == NULL || request->name == NULL || request->name_len == 0) return -1;
    if ((request->name_len == 1 && request->name[0] == '.') ||
        (request->name_len == 2 && request->name[0] == '.' && request->name[1] == '.')) {
        return 1; // Entry dengan nama sudah ada
    }

    // 1. Validate parent_inode
    if (!is_inode_allocated(request->parent_inode)) {
        return 2; // Parent folder invalid
    }
    struct EXT2Inode parent_node;
    read_inode(request->parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 2; // Parent folder invalid
    }

    // 2. Check if an entry with the same name already exists
    uint32_t total_blocks = (parent_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (total_blocks == 0 && parent_node.i_block[0] != 0) total_blocks = 1;

    for (uint32_t b = 0; b < total_blocks; b++) {
        uint32_t blk = get_node_block_id(&parent_node, b);
        if (blk == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, blk, 1);

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE) break;
            if (entry->inode != 0) {
                if (entry->name_len == request->name_len &&
                    memcmp(get_entry_name(entry), request->name, request->name_len) == 0) {
                    return 1; // Entry dengan nama sudah ada
                }
            }
            offset += entry->rec_len;
        }
    }

    // 3. Allocate new inode
    uint32_t new_inode_num = allocate_node();
    if (new_inode_num == 0) {
        return -1; // Out of inodes
    }

    uint32_t target_bgd = inode_to_bgd(new_inode_num);
    struct EXT2Inode new_node;
    memset(&new_node, 0, sizeof(struct EXT2Inode));

    uint8_t entry_file_type;

    if (request->is_directory || request->buffer_size == 0) {
        // Directory
        entry_file_type = EXT2_FT_DIR;
        uint32_t new_blk = allocate_block(target_bgd);
        if (new_blk == 0) {
            deallocate_node(new_inode_num);
            return -1;
        }

        new_node.i_mode     = EXT2_S_IFDIR;
        new_node.i_size     = BLOCK_SIZE;
        new_node.i_blocks   = 1;
        new_node.i_block[0] = new_blk;
        sync_node(&new_node, new_inode_num);

        init_directory_table(&new_node, new_inode_num, request->parent_inode);

        bgd_table.table[target_bgd].bg_used_dirs_count++;
        write_blocks(&bgd_table, 2, 1);
    } else {
        // Regular file
        entry_file_type = EXT2_FT_REG_FILE;
        new_node.i_mode = EXT2_S_IFREG;
        new_node.i_size = request->buffer_size;
        allocate_node_blocks(request->buf, &new_node, target_bgd);
        sync_node(&new_node, new_inode_num);
    }

    // 4. Insert new directory entry into parent directory
    uint16_t req_rec_len = get_entry_record_len(request->name_len);
    bool inserted = false;

    for (uint32_t b = 0; b < total_blocks; b++) {
        uint32_t blk = get_node_block_id(&parent_node, b);
        if (blk == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, blk, 1);

        uint32_t offset = 0;
        uint32_t last_offset = 0;
        struct EXT2DirectoryEntry *last_entry = NULL;

        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
            if (entry->rec_len == 0) {
                // Free space at offset
                if (offset + req_rec_len <= BLOCK_SIZE) {
                    entry->inode     = new_inode_num;
                    entry->rec_len   = req_rec_len;
                    entry->name_len  = request->name_len;
                    entry->file_type = entry_file_type;
                    char *name_dst   = get_entry_name(entry);
                    memcpy(name_dst, request->name, request->name_len);
                    write_blocks(&buf, blk, 1);
                    inserted = true;
                }
                break;
            }

            if (entry->inode == 0 && entry->rec_len >= req_rec_len) {
                // Reusing a deleted slot
                entry->inode     = new_inode_num;
                entry->name_len  = request->name_len;
                entry->file_type = entry_file_type;
                char *name_dst   = get_entry_name(entry);
                memcpy(name_dst, request->name, request->name_len);
                write_blocks(&buf, blk, 1);
                inserted = true;
                break;
            }

            last_offset = offset;
            last_entry = entry;
            offset += entry->rec_len;
        }

        if (inserted) break;

        // Try expanding from last entry if it has extra space
        if (last_entry != NULL && offset == BLOCK_SIZE) {
            uint16_t actual_last_len = get_entry_record_len(last_entry->name_len);
            if (last_entry->rec_len >= actual_last_len + req_rec_len) {
                uint16_t remain = last_entry->rec_len - actual_last_len;
                last_entry->rec_len = actual_last_len;

                struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry*) (buf.buf + last_offset + actual_last_len);
                new_entry->inode     = new_inode_num;
                new_entry->rec_len   = remain;
                new_entry->name_len  = request->name_len;
                new_entry->file_type = entry_file_type;
                char *name_dst       = get_entry_name(new_entry);
                memcpy(name_dst, request->name, request->name_len);

                write_blocks(&buf, blk, 1);
                inserted = true;
                break;
            }
        }
    }

    if (!inserted) {
        // Need to allocate a new block for parent directory
        uint32_t new_parent_blk = allocate_block(inode_to_bgd(request->parent_inode));
        if (new_parent_blk == 0) {
            return -1;
        }

        struct BlockBuffer new_dir_blk;
        memset(&new_dir_blk, 0, BLOCK_SIZE);
        struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry*) new_dir_blk.buf;
        new_entry->inode     = new_inode_num;
        new_entry->rec_len   = BLOCK_SIZE;
        new_entry->name_len  = request->name_len;
        new_entry->file_type = entry_file_type;
        char *name_dst       = get_entry_name(new_entry);
        memcpy(name_dst, request->name, request->name_len);
        write_blocks(&new_dir_blk, new_parent_blk, 1);

        // Append block to parent inode
        if (total_blocks < 12) {
            parent_node.i_block[total_blocks] = new_parent_blk;
        }
        parent_node.i_size += BLOCK_SIZE;
        parent_node.i_blocks += 1;
        sync_node(&parent_node, request->parent_inode);
    }

    return 0; // Success
}

int8_t delete(struct EXT2DriverRequest request) {
    // 0. Safety checks
    if (request.name == NULL || request.name_len == 0) return 1;
    if ((request.name_len == 1 && request.name[0] == '.') ||
        (request.name_len == 2 && request.name[0] == '.' && request.name[1] == '.')) {
        return -1; // Cannot delete . or ..
    }

    // 1. Validate parent_inode
    if (!is_inode_allocated(request.parent_inode)) {
        return 3; // Parent folder invalid
    }
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3; // Parent folder invalid
    }

    // 2. Search for entry in parent directory
    uint32_t total_blocks = (parent_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (total_blocks == 0 && parent_node.i_block[0] != 0) total_blocks = 1;

    uint32_t target_inode_num = 0;
    uint32_t found_blk = 0;
    uint32_t found_offset = 0;

    for (uint32_t b = 0; b < total_blocks; b++) {
        uint32_t blk = get_node_block_id(&parent_node, b);
        if (blk == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, blk, 1);

        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (buf.buf + offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > BLOCK_SIZE) break;
            if (entry->inode != 0) {
                if (entry->name_len == request.name_len &&
                    memcmp(get_entry_name(entry), request.name, request.name_len) == 0) {
                    target_inode_num = entry->inode;
                    found_blk = blk;
                    found_offset = offset;
                    break;
                }
            }
            offset += entry->rec_len;
        }
        if (target_inode_num != 0) break;
    }

    if (target_inode_num == 0) {
        return 1; // Entry tidak ditemukan
    }

    if (target_inode_num == 1) {
        return -1; // Root directory cannot be deleted
    }

    // 3. Read target inode & validate
    struct EXT2Inode target_node;
    read_inode(target_inode_num, &target_node);

    if (request.is_directory || (target_node.i_mode & EXT2_S_IFDIR) != 0) {
        if (!is_directory_empty(target_inode_num)) {
            return 2; // Folder yang akan dihapus tidak kosong
        }
        deallocate_blocks(target_node.i_block, target_node.i_blocks);
        deallocate_node(target_inode_num);

        uint32_t target_bgd = inode_to_bgd(target_inode_num);
        if (bgd_table.table[target_bgd].bg_used_dirs_count > 0) {
            bgd_table.table[target_bgd].bg_used_dirs_count--;
            write_blocks(&bgd_table, 2, 1);
        }
    } else {
        deallocate_blocks(target_node.i_block, target_node.i_blocks);
        deallocate_node(target_inode_num);
    }

    // 4. Mark directory entry as deleted in parent directory
    struct BlockBuffer dir_buf;
    read_blocks(&dir_buf, found_blk, 1);
    struct EXT2DirectoryEntry *del_entry = (struct EXT2DirectoryEntry*) (dir_buf.buf + found_offset);
    del_entry->inode = 0;
    write_blocks(&dir_buf, found_blk, 1);

    return 0; // Success
}
