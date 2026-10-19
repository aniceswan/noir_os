#ifndef _EXT2_H
#define _EXT2_H

#include "header/driver/disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/stdtype.h"

/* -- IF2130 File System constants -- */
#define BOOT_SECTOR 0 // legacy from FAT32 filesystem IF2130 OS
#define DISK_SPACE 4194304u // 4MB disk space (because our disk or storage.bin is 4MB)
#define EXT2_SUPER_MAGIC 0xEF53 // this indicating that the filesystem used by OS is ext2
#define INODE_SIZE sizeof(struct EXT2Inode) // size of inode
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // number of inode per block (512 / 128 = 4)
#define GROUPS_COUNT ((BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u) // number of groups in the filesystem
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // number of blocks per group (1024)
#define INODES_TABLE_BLOCK_COUNT 16u 
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // number of inodes per group (64)

/**
 * inodes constant 
 * - reference: https://www.nongnu.org/ext2-doc/ext2.html#inode-table
 */
#define EXT2_S_IFREG 0x8000 // regular file 
#define EXT2_S_IFDIR 0x4000 // directory

/* FILE TYPE CONSTANT*/
/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 * - Table 4.2. Defined Inode File Type Values
 */
#define EXT2_FT_UNKNOWN  0 // Unknown File Type
#define EXT2_FT_REG_FILE 1 // Regular File
#define EXT2_FT_DIR      2 // Directory
#define EXT2_FT_CHRDEV   3 // Character Special File

/**
 * EXT2DriverRequest
 */
struct EXT2DriverRequest {
    void     *buf; 
    char     *name; 
    uint8_t  name_len; 
    uint32_t parent_inode; 
    uint32_t buffer_size; 
    bool     is_directory; 
} __attribute__((packed));

/**
 * EXT2Superblock: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#superblock
 */
struct EXT2Superblock {
    uint32_t s_inodes_count;        // total number of inodes (512)
    uint32_t s_blocks_count;        // total number of blocks (8192)
    uint32_t s_r_blocks_count;      // reserved blocks count
    uint32_t s_free_blocks_count;   // total free blocks
    uint32_t s_free_inodes_count;   // total free inodes
    uint32_t s_first_data_block;    // id of block containing superblock (1)
    uint32_t s_first_ino;           // first usable inode (1)
    uint32_t s_blocks_per_group;    // blocks per group (1024)
    uint32_t s_frags_per_group;     // frags per group (1024)
    uint32_t s_inodes_per_group;    // inodes per group (64)
    uint16_t s_magic;               // 0xEF53
    uint8_t  s_prealloc_blocks; 
    uint8_t  s_prealloc_dir_blocks; 
} __attribute__((packed));

/**
 * EXT2BlockGroupDescriptor
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptor {
    uint32_t bg_block_bitmap; 
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3]; // 12 bytes of reserved space
} __attribute__((packed));

struct EXT2BlockGroupDescriptorTable {
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT]; 
};

/**
 * EXT2Inode (128 bytes)
 */
struct EXT2Inode {
    uint16_t i_mode;        // offset 0 (2 bytes)
    uint32_t i_size;        // offset 2 (4 bytes)
    uint8_t  _reserved0[22];// offset 6 (22 bytes: timestamps, uid, gid)
    uint32_t i_blocks;      // offset 28 (4 bytes)
    uint8_t  _reserved1[8]; // offset 32 (8 bytes: flags, osd1)
    uint32_t i_block[15];   // offset 40 (60 bytes: 12 direct, 1 indirect, 1 doubly-indirect, 1 triply-indirect)
    uint8_t  _reserved2[28];// offset 100 (28 bytes: generation, acls, osd2)
} __attribute__((packed));

struct EXT2InodeTable {
    struct EXT2Inode table[INODES_PER_GROUP]; 
};

/**
 * EXT2DirectoryEntry
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 */
struct EXT2DirectoryEntry {
    uint32_t inode;     // Inode number of the entry (0 if unused)
    uint16_t rec_len;   // Record length (distance to next entry)
    uint8_t  name_len;  // Length of file name
    uint8_t  file_type; // File type (1: regular file, 2: directory)
} __attribute__((packed));

/* =================== REGULAR FUNCTION ============================ */

char *get_entry_name(void *entry);
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset);
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry);
uint16_t get_entry_record_len(uint8_t name_len);
uint32_t get_dir_first_child_offset(void *ptr);

/* =================== MAIN FUNCTION OF EXT2 FILESYSTEM ============================ */

uint32_t inode_to_bgd(uint32_t inode);
uint32_t inode_to_local(uint32_t inode);
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode);
bool is_empty_storage(void);
void create_ext2(void);
void initialize_filesystem_ext2(void);
bool is_directory_empty(uint32_t inode);

/* =============================== CRUD FUNC ======================================== */

int8_t read_directory(struct EXT2DriverRequest *prequest);
int8_t read(struct EXT2DriverRequest request);
int8_t write(struct EXT2DriverRequest *request);
int8_t delete(struct EXT2DriverRequest request);

/* =============================== MEMORY ========================================== */

uint32_t allocate_node(void); 
void deallocate_node(uint32_t inode);
void deallocate_blocks(void *loc, uint32_t blocks);
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded);
void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd);
void sync_node(struct EXT2Inode *node, uint32_t inode);

#endif
