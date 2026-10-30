#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

#define DIRECT_BLOCK_COUNT 12u
#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define EXT2_NAME_MAX 255u

static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

static void load_superblock(void) {
    struct BlockBuffer buffer;
    read_blocks(&buffer, 1, 1);
    memcpy(&superblock, buffer.buf, sizeof(superblock));
}

static void store_superblock(void) {
    struct BlockBuffer buffer;
    memset(&buffer, 0, BLOCK_SIZE);
    memcpy(buffer.buf, &superblock, sizeof(superblock));
    write_blocks(&buffer, 1, 1);
}

static void load_bgd_table(void) {
    struct BlockBuffer buffer;
    read_blocks(&buffer, 2, 1);
    memcpy(&bgd_table, buffer.buf, sizeof(bgd_table));
}

static void store_bgd_table(void) {
    struct BlockBuffer buffer;
    memset(&buffer, 0, BLOCK_SIZE);
    memcpy(buffer.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&buffer, 2, 1);
}

static void store_bgd(uint32_t bgd) {
    struct BlockBuffer buffer;
    read_blocks(&buffer, 2, 1);
    memcpy(buffer.buf + bgd * sizeof(struct EXT2BlockGroupDescriptor),
           &bgd_table.table[bgd], sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&buffer, 2, 1);
}

static bool bitmap_get(const struct BlockBuffer *bitmap, uint32_t index) {
    return (bitmap->buf[index / 8] & (1u << (index % 8))) != 0;
}

static void bitmap_set(struct BlockBuffer *bitmap, uint32_t index) {
    bitmap->buf[index / 8] |= (uint8_t) (1u << (index % 8));
}

static void bitmap_clear(struct BlockBuffer *bitmap, uint32_t index) {
    bitmap->buf[index / 8] &= (uint8_t) ~(1u << (index % 8));
}

static void read_bitmap(uint32_t block, struct BlockBuffer *bitmap) {
    read_blocks(bitmap, block, 1);
}

static void write_bitmap(uint32_t block, struct BlockBuffer *bitmap) {
    write_blocks(bitmap, block, 1);
}

uint32_t inode_to_bgd(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) % INODES_PER_GROUP;
}

char *get_entry_name(void *entry) {
    return (char *) ((uint8_t *) entry + sizeof(struct EXT2DirectoryEntry));
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (uint16_t) ((sizeof(struct EXT2DirectoryEntry) + name_len + 3u) & ~3u);
}

uint32_t get_dir_first_child_offset(void *ptr) {
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry = get_directory_entry(ptr, offset);
    offset += entry->rec_len;
    entry = get_directory_entry(ptr, offset);
    offset += entry->rec_len;
    return offset;
}

static uint32_t allocate_block(uint32_t prefered_bgd) {
    for (uint32_t step = 0; step < GROUPS_COUNT; step++) {
        uint32_t bgd = (prefered_bgd + step) % GROUPS_COUNT;
        if (bgd_table.table[bgd].bg_free_blocks_count == 0) continue;

        struct BlockBuffer bitmap;
        read_bitmap(bgd_table.table[bgd].bg_block_bitmap, &bitmap);
        for (uint32_t local = 0; local < BLOCKS_PER_GROUP; local++) {
            if (!bitmap_get(&bitmap, local)) {
                bitmap_set(&bitmap, local);
                write_bitmap(bgd_table.table[bgd].bg_block_bitmap, &bitmap);
                bgd_table.table[bgd].bg_free_blocks_count--;
                if (superblock.s_free_blocks_count > 0) superblock.s_free_blocks_count--;
                store_bgd(bgd);
                return bgd * BLOCKS_PER_GROUP + local;
            }
        }
    }
    return 0;
}

static void free_block(uint32_t block) {
    if (block == 0) return;
    uint32_t bgd = block / BLOCKS_PER_GROUP;
    uint32_t local = block % BLOCKS_PER_GROUP;

    struct BlockBuffer bitmap;
    read_bitmap(bgd_table.table[bgd].bg_block_bitmap, &bitmap);
    if (bitmap_get(&bitmap, local)) {
        bitmap_clear(&bitmap, local);
        write_bitmap(bgd_table.table[bgd].bg_block_bitmap, &bitmap);
        bgd_table.table[bgd].bg_free_blocks_count++;
        superblock.s_free_blocks_count++;
        store_bgd(bgd);
    }
}

uint32_t allocate_node(void) {
    for (uint32_t bgd = 0; bgd < GROUPS_COUNT; bgd++) {
        if (bgd_table.table[bgd].bg_free_inodes_count == 0) continue;

        struct BlockBuffer bitmap;
        read_bitmap(bgd_table.table[bgd].bg_inode_bitmap, &bitmap);
        for (uint32_t local = 0; local < INODES_PER_GROUP; local++) {
            if (!bitmap_get(&bitmap, local)) {
                bitmap_set(&bitmap, local);
                write_bitmap(bgd_table.table[bgd].bg_inode_bitmap, &bitmap);
                bgd_table.table[bgd].bg_free_inodes_count--;
                if (superblock.s_free_inodes_count > 0) superblock.s_free_inodes_count--;
                store_bgd(bgd);
                return bgd * INODES_PER_GROUP + local + 1;
            }
        }
    }
    return 0;
}

static void free_inode_bit(uint32_t inode) {
    if (inode == 0) return;
    uint32_t bgd = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);

    struct BlockBuffer bitmap;
    read_bitmap(bgd_table.table[bgd].bg_inode_bitmap, &bitmap);
    if (bitmap_get(&bitmap, local)) {
        bitmap_clear(&bitmap, local);
        write_bitmap(bgd_table.table[bgd].bg_inode_bitmap, &bitmap);
        bgd_table.table[bgd].bg_free_inodes_count++;
        superblock.s_free_inodes_count++;
        store_bgd(bgd);
    }
}

static void read_inode(uint32_t inode, struct EXT2Inode *node) {
    uint32_t bgd = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    uint32_t table_block = bgd_table.table[bgd].bg_inode_table
                         + local / INODES_PER_TABLE;
    uint32_t offset = (local % INODES_PER_TABLE) * INODE_SIZE;

    struct BlockBuffer buffer;
    read_blocks(&buffer, table_block, 1);
    memcpy(node, buffer.buf + offset, INODE_SIZE);
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    uint32_t bgd = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    uint32_t table_block = bgd_table.table[bgd].bg_inode_table
                         + local / INODES_PER_TABLE;
    uint32_t offset = (local % INODES_PER_TABLE) * INODE_SIZE;

    struct BlockBuffer buffer;
    read_blocks(&buffer, table_block, 1);
    memcpy(buffer.buf + offset, node, INODE_SIZE);
    write_blocks(&buffer, table_block, 1);
}

static uint32_t node_get_data_block(struct EXT2Inode *node, uint32_t index,
                                    bool allocate, uint32_t prefered_bgd) {
    if (index < DIRECT_BLOCK_COUNT) {
        if (node->i_block[index] == 0 && allocate) {
            uint32_t block = allocate_block(prefered_bgd);
            if (block != 0) {
                node->i_block[index] = block;
                node->i_blocks++;
            }
        }
        return node->i_block[index];
    }
    index -= DIRECT_BLOCK_COUNT;

    if (index < POINTERS_PER_BLOCK) {
        if (node->i_block[12] == 0) {
            if (!allocate) return 0;
            uint32_t block = allocate_block(prefered_bgd);
            if (block == 0) return 0;
            node->i_block[12] = block;
            node->i_blocks++;
            struct BlockBuffer zero;
            memset(&zero, 0, BLOCK_SIZE);
            write_blocks(&zero, block, 1);
        }
        struct BlockBuffer indirect;
        read_blocks(&indirect, node->i_block[12], 1);
        uint32_t *pointers = (uint32_t *) indirect.buf;
        if (pointers[index] == 0 && allocate) {
            uint32_t block = allocate_block(prefered_bgd);
            if (block != 0) {
                pointers[index] = block;
                node->i_blocks++;
                write_blocks(&indirect, node->i_block[12], 1);
            }
        }
        return pointers[index];
    }
    index -= POINTERS_PER_BLOCK;

    if (index < POINTERS_PER_BLOCK * POINTERS_PER_BLOCK) {
        if (node->i_block[13] == 0) {
            if (!allocate) return 0;
            uint32_t block = allocate_block(prefered_bgd);
            if (block == 0) return 0;
            node->i_block[13] = block;
            node->i_blocks++;
            struct BlockBuffer zero;
            memset(&zero, 0, BLOCK_SIZE);
            write_blocks(&zero, block, 1);
        }
        struct BlockBuffer first;
        read_blocks(&first, node->i_block[13], 1);
        uint32_t *first_pointers = (uint32_t *) first.buf;
        uint32_t major = index / POINTERS_PER_BLOCK;
        uint32_t minor = index % POINTERS_PER_BLOCK;

        if (first_pointers[major] == 0) {
            if (!allocate) return 0;
            uint32_t block = allocate_block(prefered_bgd);
            if (block == 0) return 0;
            first_pointers[major] = block;
            node->i_blocks++;
            write_blocks(&first, node->i_block[13], 1);
            struct BlockBuffer zero;
            memset(&zero, 0, BLOCK_SIZE);
            write_blocks(&zero, block, 1);
        }
        struct BlockBuffer second;
        read_blocks(&second, first_pointers[major], 1);
        uint32_t *second_pointers = (uint32_t *) second.buf;
        if (second_pointers[minor] == 0 && allocate) {
            uint32_t block = allocate_block(prefered_bgd);
            if (block != 0) {
                second_pointers[minor] = block;
                node->i_blocks++;
                write_blocks(&second, first_pointers[major], 1);
            }
        }
        return second_pointers[minor];
    }
    return 0;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    uint32_t block_count = (node->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = node_get_data_block(node, i, true, prefered_bgd);
        if (block == 0) break;
        if (ptr == NULL) continue;

        uint32_t remaining = node->i_size - i * BLOCK_SIZE;
        uint32_t chunk = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        struct BlockBuffer buffer;
        memset(&buffer, 0, BLOCK_SIZE);
        memcpy(buffer.buf, (const uint8_t *) ptr + i * BLOCK_SIZE, chunk);
        write_blocks(&buffer, block, 1);
    }
}

static uint32_t dir_read_bytes(struct EXT2Inode *node, uint32_t offset,
                               void *dst, uint32_t len) {
    uint32_t copied = 0;
    while (copied < len && offset + copied < node->i_size) {
        uint32_t block_index = (offset + copied) / BLOCK_SIZE;
        uint32_t in_block = (offset + copied) % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        if (chunk > len - copied) chunk = len - copied;
        if (chunk > node->i_size - (offset + copied))
            chunk = node->i_size - (offset + copied);

        uint32_t block = node_get_data_block(node, block_index, false, 0);
        if (block == 0) break;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);
        memcpy((uint8_t *) dst + copied, buffer.buf + in_block, chunk);
        copied += chunk;
    }
    return copied;
}

static uint32_t dir_write_bytes(struct EXT2Inode *node, uint32_t offset,
                                const void *src, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        uint32_t block_index = (offset + written) / BLOCK_SIZE;
        uint32_t in_block = (offset + written) % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        if (chunk > len - written) chunk = len - written;

        uint32_t block = node_get_data_block(node, block_index, false, 0);
        if (block == 0) break;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);
        memcpy(buffer.buf + in_block, (const uint8_t *) src + written, chunk);
        write_blocks(&buffer, block, 1);
        written += chunk;
    }
    return written;
}

struct ext2_dir_scan {
    bool found;
    bool name_matched;
    uint32_t offset;
    struct EXT2DirectoryEntry entry;
};

static void scan_directory(struct EXT2Inode *dir, const char *name,
                           uint8_t name_len, uint8_t file_type,
                           struct ext2_dir_scan *result) {
    result->found = false;
    result->name_matched = false;
    result->offset = 0;

    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= dir->i_size) {
        struct EXT2DirectoryEntry entry;
        if (dir_read_bytes(dir, offset, &entry, sizeof(entry)) != sizeof(entry))
            break;
        if (entry.rec_len == 0) break;

        if (entry.inode != 0 && entry.name_len == name_len) {
            char entry_name[EXT2_NAME_MAX + 1];
            if (dir_read_bytes(dir, offset + sizeof(entry), entry_name, name_len) == name_len) {
                if (memcmp(entry_name, name, name_len) == 0) {
                    result->name_matched = true;
                    if (entry.file_type == file_type) {
                        result->found = true;
                        result->offset = offset;
                        result->entry = entry;
                        return;
                    }
                }
            }
        }
        offset += entry.rec_len;
    }
}

static int8_t add_directory_entry(uint32_t parent_inode, struct EXT2Inode *parent,
                                  const char *name, uint8_t name_len,
                                  uint8_t file_type, uint32_t child_inode) {
    uint16_t needed = get_entry_record_len(name_len);
    uint32_t used = parent->i_size;
    uint32_t prefered_bgd = inode_to_bgd(parent_inode);

    uint32_t block_count = (used + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = node_get_data_block(parent, i, false, prefered_bgd);
        if (block == 0) return -1;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);

        uint32_t used_in_block = (i == block_count - 1) ? used - i * BLOCK_SIZE : BLOCK_SIZE;
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= used_in_block) {
            struct EXT2DirectoryEntry *entry = get_directory_entry(buffer.buf, offset);
            if (entry->rec_len == 0) break;

            if (entry->inode == 0 && entry->rec_len >= needed) {
                uint16_t old_rec_len = entry->rec_len;
                entry->inode = child_inode;
                entry->name_len = name_len;
                entry->file_type = file_type;
                entry->rec_len = (uint16_t) ((old_rec_len - needed >= 12) ? needed : old_rec_len);
                memcpy(get_entry_name(entry), name, name_len);
                if (entry->rec_len != old_rec_len) {
                    struct EXT2DirectoryEntry *rest = get_next_directory_entry(entry);
                    rest->inode = 0;
                    rest->rec_len = old_rec_len - needed;
                    rest->name_len = 0;
                    rest->file_type = 0;
                }
                write_blocks(&buffer, block, 1);
                return 0;
            }
            offset += entry->rec_len;
        }
    }

    uint32_t last_block_index = (used == 0) ? 0 : (used - 1) / BLOCK_SIZE;
    uint32_t used_in_last = used - last_block_index * BLOCK_SIZE;

    if (used_in_last + needed <= BLOCK_SIZE) {
        uint32_t block = node_get_data_block(parent, last_block_index, true, prefered_bgd);
        if (block == 0) return -1;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);
        struct EXT2DirectoryEntry *entry = get_directory_entry(buffer.buf, used_in_last);
        entry->inode = child_inode;
        entry->rec_len = needed;
        entry->name_len = name_len;
        entry->file_type = file_type;
        memcpy(get_entry_name(entry), name, name_len);
        write_blocks(&buffer, block, 1);
        parent->i_size = used + needed;
        return 0;
    }

    uint32_t new_block_index = (used + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t block = node_get_data_block(parent, new_block_index, true, prefered_bgd);
    if (block == 0) return -1;
    struct BlockBuffer buffer;
    memset(&buffer, 0, BLOCK_SIZE);
    struct EXT2DirectoryEntry *entry = get_directory_entry(buffer.buf, 0);
    entry->inode = child_inode;
    entry->rec_len = needed;
    entry->name_len = name_len;
    entry->file_type = file_type;
    memcpy(get_entry_name(entry), name, name_len);
    write_blocks(&buffer, block, 1);
    parent->i_size = used + needed;
    return 0;
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    if (node->i_block[0] == 0) {
        uint32_t block = allocate_block(inode_to_bgd(inode));
        if (block == 0) return;
        node->i_block[0] = block;
        node->i_blocks++;
    }

    struct BlockBuffer buffer;
    memset(&buffer, 0, BLOCK_SIZE);

    struct EXT2DirectoryEntry *self = get_directory_entry(buffer.buf, 0);
    self->inode = inode;
    self->rec_len = get_entry_record_len(1);
    self->name_len = 1;
    self->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(self), ".", 1);

    struct EXT2DirectoryEntry *parent = get_next_directory_entry(self);
    parent->inode = parent_inode;
    parent->rec_len = get_entry_record_len(2);
    parent->name_len = 2;
    parent->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(parent), "..", 2);

    write_blocks(&buffer, node->i_block[0], 1);
    node->i_size = get_entry_record_len(1) + get_entry_record_len(2);
}

static void free_indirect(uint32_t block, uint32_t depth) {
    if (block == 0) return;
    uint32_t pointers[POINTERS_PER_BLOCK];
    read_blocks(pointers, block, 1);
    for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
        if (pointers[i] == 0) continue;
        if (depth > 1) free_indirect(pointers[i], depth - 1);
        else free_block(pointers[i]);
    }
    free_block(block);
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    uint32_t *locations = (uint32_t *) loc;

    uint32_t direct = blocks < DIRECT_BLOCK_COUNT ? blocks : DIRECT_BLOCK_COUNT;
    for (uint32_t i = 0; i < direct; i++) {
        if (locations[i] == 0) continue;
        free_block(locations[i]);
        locations[i] = 0;
    }

    if (blocks > DIRECT_BLOCK_COUNT && locations[12] != 0) {
        uint32_t remaining = blocks - DIRECT_BLOCK_COUNT;
        uint32_t count = remaining < POINTERS_PER_BLOCK ? remaining : POINTERS_PER_BLOCK;
        uint32_t pointers[POINTERS_PER_BLOCK];
        read_blocks(pointers, locations[12], 1);
        for (uint32_t i = 0; i < count; i++) {
            if (pointers[i] == 0) continue;
            free_block(pointers[i]);
        }
        free_block(locations[12]);
        locations[12] = 0;
    }

    if (blocks > DIRECT_BLOCK_COUNT + POINTERS_PER_BLOCK && locations[13] != 0) {
        uint32_t remaining = blocks - DIRECT_BLOCK_COUNT - POINTERS_PER_BLOCK;
        uint32_t pointers[POINTERS_PER_BLOCK];
        read_blocks(pointers, locations[13], 1);
        for (uint32_t i = 0; i < POINTERS_PER_BLOCK && remaining > 0; i++) {
            if (pointers[i] != 0) {
                uint32_t count = remaining < POINTERS_PER_BLOCK ? remaining : POINTERS_PER_BLOCK;
                uint32_t children[POINTERS_PER_BLOCK];
                read_blocks(children, pointers[i], 1);
                for (uint32_t j = 0; j < count; j++) {
                    if (children[j] == 0) continue;
                    free_block(children[j]);
                }
                free_block(pointers[i]);
            }
            remaining = remaining > POINTERS_PER_BLOCK ? remaining - POINTERS_PER_BLOCK : 0;
        }
        free_block(locations[13]);
        locations[13] = 0;
    }

    if (blocks > DIRECT_BLOCK_COUNT + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK
        && locations[14] != 0) {
        free_indirect(locations[14], 3);
        locations[14] = 0;
    }
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks,
                          struct BlockBuffer *bitmap, uint32_t depth,
                          uint32_t *last_bgd, bool bgd_loaded) {
    (void) bgd_loaded;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t block = locations[i];
        if (block == 0) continue;
        locations[i] = 0;
        if (depth > 0) {
            free_indirect(block, depth);
        } else {
            free_block(block);
        }
        *last_bgd = block / BLOCKS_PER_GROUP;
    }
    if (bitmap != NULL) {
        read_bitmap(bgd_table.table[*last_bgd].bg_block_bitmap, bitmap);
    }
    return *last_bgd;
}

static void release_inode_node(struct EXT2Inode *node, uint32_t inode) {
    uint32_t block_count = (node->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (block_count > 0) deallocate_blocks(node->i_block, block_count);

    free_inode_bit(inode);
    memset(node, 0, INODE_SIZE);
    sync_node(node, inode);
}

void deallocate_node(uint32_t inode) {
    struct EXT2Inode node;
    read_inode(inode, &node);
    release_inode_node(&node, inode);
}

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_blocks_count = DISK_SPACE / BLOCK_SIZE;
    superblock.s_r_blocks_count = 0;
    superblock.s_free_blocks_count = 0;
    superblock.s_free_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_first_data_block = 0;
    superblock.s_first_ino = 1;
    superblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group = INODES_PER_GROUP;
    superblock.s_magic = EXT2_SUPER_MAGIC;
    superblock.s_prealloc_blocks = 0;
    superblock.s_prealloc_dir_blocks = 0;

    for (uint32_t bgd = 0; bgd < GROUPS_COUNT; bgd++) {
        uint32_t group_start = bgd * BLOCKS_PER_GROUP;
        uint32_t block_bitmap = group_start + ((bgd == 0) ? 3u : 0u);
        uint32_t inode_bitmap = block_bitmap + 1;
        uint32_t inode_table = block_bitmap + 2;
        uint32_t reserved = ((bgd == 0) ? 3u : 0u) + 2u + INODES_TABLE_BLOCK_COUNT;

        bgd_table.table[bgd].bg_block_bitmap = block_bitmap;
        bgd_table.table[bgd].bg_inode_bitmap = inode_bitmap;
        bgd_table.table[bgd].bg_inode_table = inode_table;
        bgd_table.table[bgd].bg_free_blocks_count = (uint16_t) (BLOCKS_PER_GROUP - reserved);
        bgd_table.table[bgd].bg_free_inodes_count = INODES_PER_GROUP;
        bgd_table.table[bgd].bg_used_dirs_count = 0;
        bgd_table.table[bgd].bg_pad = 0;
        bgd_table.table[bgd].bg_reserved[0] = 0;
        bgd_table.table[bgd].bg_reserved[1] = 0;
        bgd_table.table[bgd].bg_reserved[2] = 0;

        struct BlockBuffer block_bitmap_buffer;
        struct BlockBuffer inode_bitmap_buffer;
        memset(&block_bitmap_buffer, 0, BLOCK_SIZE);
        memset(&inode_bitmap_buffer, 0, BLOCK_SIZE);
        for (uint32_t i = 0; i < reserved; i++)
            bitmap_set(&block_bitmap_buffer, i);
        write_bitmap(block_bitmap, &block_bitmap_buffer);
        write_bitmap(inode_bitmap, &inode_bitmap_buffer);

        superblock.s_free_blocks_count += bgd_table.table[bgd].bg_free_blocks_count;
    }

    store_superblock();
    store_bgd_table();

    uint32_t root_inode = allocate_node();
    struct EXT2Inode root_node;
    memset(&root_node, 0, INODE_SIZE);
    root_node.i_mode = EXT2_S_IFDIR;
    init_directory_table(&root_node, root_inode, root_inode);
    sync_node(&root_node, root_inode);

    store_superblock();
    store_bgd_table();
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        load_superblock();
        load_bgd_table();
    }
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode node;
    read_inode(inode, &node);
    if ((node.i_mode & EXT2_S_IFDIR) == 0) return false;

    uint32_t offset = 0;
    uint32_t index = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= node.i_size) {
        struct EXT2DirectoryEntry entry;
        if (dir_read_bytes(&node, offset, &entry, sizeof(entry)) != sizeof(entry))
            break;
        if (entry.rec_len == 0) break;
        if (index >= 2 && entry.inode != 0) return false;
        offset += entry.rec_len;
        index++;
    }
    return true;
}

static int8_t read_directory_data(uint32_t inode, void *buf, uint32_t buffer_size) {
    struct EXT2Inode node;
    read_inode(inode, &node);
    if ((node.i_mode & EXT2_S_IFDIR) == 0) return 1;
    if (buffer_size < node.i_size) return -1;

    uint32_t block_count = (node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = node_get_data_block(&node, i, false, 0);
        if (block == 0) return -1;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);
        uint32_t remaining = node.i_size - i * BLOCK_SIZE;
        uint32_t chunk = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        memcpy((uint8_t *) buf + i * BLOCK_SIZE, buffer.buf, chunk);
    }
    return 0;
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (prequest == NULL || prequest->buf == NULL) return -1;
    if (prequest->parent_inode == 0 || prequest->parent_inode > superblock.s_inodes_count)
        return 3;

    if (prequest->name == NULL || prequest->name_len == 0) {
        int8_t retcode = read_directory_data(prequest->parent_inode,
                                             prequest->buf, prequest->buffer_size);
        return retcode;
    }

    struct EXT2Inode parent;
    read_inode(prequest->parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 3;

    struct ext2_dir_scan scan;
    scan_directory(&parent, prequest->name, prequest->name_len, EXT2_FT_DIR, &scan);
    if (!scan.found) {
        if (scan.name_matched) return 1;
        return 2;
    }

    int8_t retcode = read_directory_data(scan.entry.inode,
                                         prequest->buf, prequest->buffer_size);
    return retcode;
}

int8_t read(struct EXT2DriverRequest request) {
    /* Note: request.buf may legitimately be virtual address 0 for the
     * initial user program, so it is not validated here. */
    if (request.name == NULL) return -1;
    if (request.parent_inode == 0 || request.parent_inode > superblock.s_inodes_count)
        return 4;

    struct EXT2Inode parent;
    read_inode(request.parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 4;

    struct ext2_dir_scan scan;
    scan_directory(&parent, request.name, request.name_len, EXT2_FT_REG_FILE, &scan);
    if (!scan.found) {
        if (scan.name_matched) return 1;
        return 3;
    }

    struct EXT2Inode node;
    read_inode(scan.entry.inode, &node);
    if ((node.i_mode & EXT2_S_IFREG) == 0) return 1;
    if (request.buffer_size < node.i_size) return 2;

    uint32_t block_count = (node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = node_get_data_block(&node, i, false, 0);
        if (block == 0) return -1;
        struct BlockBuffer buffer;
        read_blocks(&buffer, block, 1);
        uint32_t remaining = node.i_size - i * BLOCK_SIZE;
        uint32_t chunk = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        memcpy((uint8_t *) request.buf + i * BLOCK_SIZE, buffer.buf, chunk);
    }
    return 0;
}

int8_t get_entry_info(const char *name, uint8_t name_len, uint32_t parent_inode,
                      uint32_t *inode_out, uint8_t *file_type_out) {
    if (name == NULL || inode_out == NULL) return -1;
    if (parent_inode == 0 || parent_inode > superblock.s_inodes_count) return 4;

    struct EXT2Inode parent;
    read_inode(parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 4;

    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= parent.i_size) {
        struct EXT2DirectoryEntry entry;
        if (dir_read_bytes(&parent, offset, &entry, sizeof(entry)) != sizeof(entry))
            break;
        if (entry.rec_len == 0) break;

        if (entry.inode != 0 && entry.name_len == name_len) {
            char entry_name[EXT2_NAME_MAX + 1];
            if (dir_read_bytes(&parent, offset + sizeof(entry), entry_name, name_len) == name_len) {
                if (memcmp(entry_name, name, name_len) == 0) {
                    *inode_out = entry.inode;
                    if (file_type_out != NULL) *file_type_out = entry.file_type;
                    return 0;
                }
            }
        }
        offset += entry.rec_len;
    }
    return 3;
}

int8_t get_file_size(const char *name, uint8_t name_len, uint32_t parent_inode, uint32_t *size_out) {
    if (name == NULL || size_out == NULL) return -1;
    if (parent_inode == 0 || parent_inode > superblock.s_inodes_count) return 4;

    struct EXT2Inode parent;
    read_inode(parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 4;

    struct ext2_dir_scan scan;
    scan_directory(&parent, name, name_len, EXT2_FT_REG_FILE, &scan);
    if (!scan.found) {
        if (scan.name_matched) return 1;
        return 3;
    }

    struct EXT2Inode node;
    read_inode(scan.entry.inode, &node);
    if ((node.i_mode & EXT2_S_IFREG) == 0) return 1;

    *size_out = node.i_size;
    return 0;
}

int8_t write(struct EXT2DriverRequest *request) {
    if (request == NULL || request->name == NULL || request->name_len == 0) return -1;
    if (request->parent_inode == 0 || request->parent_inode > superblock.s_inodes_count)
        return 2;

    struct EXT2Inode parent;
    read_inode(request->parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 2;

    bool is_directory = request->is_directory || request->buffer_size == 0;
    uint8_t file_type = is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    struct ext2_dir_scan scan;
    scan_directory(&parent, request->name, request->name_len, file_type, &scan);
    if (scan.found) return 1;

    uint32_t inode = allocate_node();
    if (inode == 0) return -1;

    struct EXT2Inode node;
    memset(&node, 0, INODE_SIZE);
    node.i_mode = is_directory ? EXT2_S_IFDIR : EXT2_S_IFREG;

    if (is_directory) {
        init_directory_table(&node, inode, request->parent_inode);
        if (node.i_block[0] == 0) {
            release_inode_node(&node, inode);
            store_superblock();
            store_bgd_table();
            return -1;
        }
    } else {
        node.i_size = request->buffer_size;
        allocate_node_blocks(request->buf, &node, inode_to_bgd(inode));
        uint32_t block_count = (node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        for (uint32_t i = 0; i < block_count; i++) {
            if (node_get_data_block(&node, i, false, 0) == 0) {
                release_inode_node(&node, inode);
                store_superblock();
                store_bgd_table();
                return -1;
            }
        }
    }

    if (add_directory_entry(request->parent_inode, &parent, request->name,
                            request->name_len, file_type, inode) != 0) {
        release_inode_node(&node, inode);
        store_superblock();
        store_bgd_table();
        return -1;
    }

    sync_node(&node, inode);
    sync_node(&parent, request->parent_inode);
    store_superblock();
    store_bgd_table();
    return 0;
}

int8_t delete(struct EXT2DriverRequest request) {
    if (request.name == NULL || request.name_len == 0) return -1;
    if (request.parent_inode == 0 || request.parent_inode > superblock.s_inodes_count)
        return 3;

    struct EXT2Inode parent;
    read_inode(request.parent_inode, &parent);
    if ((parent.i_mode & EXT2_S_IFDIR) == 0) return 3;

    uint8_t file_type = request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    struct ext2_dir_scan scan;
    scan_directory(&parent, request.name, request.name_len, file_type, &scan);
    if (!scan.found) return 1;

    if (request.is_directory && !is_directory_empty(scan.entry.inode)) return 2;

    uint8_t cleared_inode[sizeof(uint32_t)];
    memset(cleared_inode, 0, sizeof(cleared_inode));
    dir_write_bytes(&parent, scan.offset, cleared_inode, sizeof(cleared_inode));

    deallocate_node(scan.entry.inode);

    sync_node(&parent, request.parent_inode);
    store_superblock();
    store_bgd_table();
    return 0;
}
