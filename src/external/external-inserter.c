#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "header/filesystem/ext2.h"

#define STORAGE_SIZE (4 * 1024 * 1024)

// Global variable
uint8_t *image_storage;
uint8_t *file_buffer;
uint8_t *read_buffer;
bool is_replace = false;
bool use_path = false;

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t*) ptr + BLOCK_SIZE*i,
            image_storage + BLOCK_SIZE*(logical_block_address+i),
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE*(logical_block_address+i),
            (uint8_t*) ptr + BLOCK_SIZE*i,
            BLOCK_SIZE
        );
    }
}

/**
 * Dump a directory and look up one child by name. Returns its inode (0 when
 * not found).
 */
static uint32_t find_entry_inode(uint32_t parent_inode, const char *name,
                                 uint8_t name_len, uint8_t *type_out) {
    static uint8_t dir_buffer[STORAGE_SIZE];
    struct EXT2DriverRequest request = {
        .buf          = dir_buffer,
        .name         = NULL,
        .name_len     = 0,
        .parent_inode = parent_inode,
        .buffer_size  = sizeof(dir_buffer),
        .is_directory = true,
    };
    memset(dir_buffer, 0, sizeof(dir_buffer));
    if (read_directory(&request) != 0) return 0;

    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= sizeof(dir_buffer)) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (dir_buffer + offset);
        if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
        if (entry->inode != 0 && entry->name_len == name_len) {
            char *entry_name = (char *) (entry + 1);
            if (memcmp(entry_name, name, name_len) == 0) {
                if (type_out != NULL) *type_out = entry->file_type;
                return entry->inode;
            }
        }
        offset += entry->rec_len;
    }
    return 0;
}

static uint32_t create_directory(uint32_t parent_inode, const char *name, uint8_t name_len) {
    struct EXT2DriverRequest request = {
        .buf          = NULL,
        .name         = (char *) name,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = true,
    };
    if (write(&request) != 0) return 0;
    return find_entry_inode(parent_inode, name, name_len, NULL);
}

/**
 * Resolve a virtual path ("/bin/clock") creating missing directories.
 * On success, fills parent_inode and copies the final component name.
 */
static bool resolve_parent_path(const char *path, uint32_t *parent_inode_out,
                                char *name_out, uint32_t name_capacity,
                                uint8_t *name_len_out) {
    uint32_t inode = 1; // root
    const char *cursor = path;
    if (*cursor == '/') cursor++;
    if (*cursor == '\0') return false;

    char component[256];
    while (true) {
        const char *slash = strchr(cursor, '/');
        size_t length = (slash != NULL) ? (size_t) (slash - cursor) : strlen(cursor);
        if (length == 0 || length >= sizeof(component)) return false;
        memcpy(component, cursor, length);
        component[length] = '\0';

        if (slash == NULL) {
            if (length >= name_capacity) return false;
            memcpy(name_out, component, length + 1);
            *name_len_out = (uint8_t) length;
            *parent_inode_out = inode;
            return true;
        }

        uint32_t child = find_entry_inode(inode, component, (uint8_t) length, NULL);
        if (child == 0) {
            child = create_directory(inode, component, (uint8_t) length);
            if (child == 0) {
                fprintf(stderr, "inserter: cannot create directory '%s'\n", component);
                return false;
            }
            printf("Created directory /%s (inode %u)\n", component, child);
        }
        inode = child;

        cursor = slash + 1;
        if (*cursor == '\0') return false;
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (strcmp(argv[argi], "-f") == 0) {
            is_replace = true;
        } else if (strcmp(argv[argi], "-p") == 0) {
            use_path = true;
        } else {
            fprintf(stderr, "inserter: unknown option '%s'\n", argv[argi]);
            exit(1);
        }
        argi++;
    }

    if (argc - argi < 3) {
        fprintf(stderr, "Usage: inserter [-f] [-p] <file> <parent_inode|path> <storage.bin>\n");
        fprintf(stderr, "  -f : replace if the entry already exists\n");
        fprintf(stderr, "  -p : interpret the second argument as a path (creates folders)\n");
        exit(1);
    }

    const char *target_path = argv[argi];
    const char *parent_str  = argv[argi + 1];
    const char *storage_path = argv[argi + 2];

    // Read storage into memory, requiring 4 MB memory
    image_storage = malloc(STORAGE_SIZE);
    file_buffer   = malloc(STORAGE_SIZE);
    read_buffer   = malloc(STORAGE_SIZE);
    if (image_storage == NULL || file_buffer == NULL || read_buffer == NULL) {
        fprintf(stderr, "inserter: out of host memory\n");
        exit(1);
    }

    FILE *fptr = fopen(storage_path, "rb+");
    if (fptr == NULL) {
        fprintf(stderr, "inserter: failed to open storage '%s'\n", storage_path);
        exit(1);
    }
    size_t read_count = fread(image_storage, STORAGE_SIZE, 1, fptr);
    if (read_count != 1) {
        fprintf(stderr, "inserter: failed to read storage (expected 4MiB)\n");
        fclose(fptr);
        exit(1);
    }

    // Read target file, assuming file is less than 4 MiB
    FILE *fptr_target = fopen(target_path, "rb");
    if (fptr_target == NULL) {
        fprintf(stderr, "inserter: cannot open input file '%s'\n", target_path);
        fclose(fptr);
        exit(1);
    }
    size_t filesize = fread(file_buffer, 1, STORAGE_SIZE, fptr_target);
    fclose(fptr_target);

    // EXT2 stores flat names: use the basename of the target path
    const char *base = strrchr(target_path, '/');
    if (base == NULL) base = target_path;
    else base++;

    char resolved_name[256];
    uint8_t filename_length = (uint8_t) strlen(base);
    uint32_t parent_inode = 0;

    initialize_filesystem_ext2();

    if (use_path) {
        if (!resolve_parent_path(parent_str, &parent_inode, resolved_name,
                                 sizeof(resolved_name), &filename_length)) {
            fprintf(stderr, "inserter: cannot resolve path '%s'\n", parent_str);
            fclose(fptr);
            exit(1);
        }
    } else {
        strncpy(resolved_name, base, sizeof(resolved_name) - 1);
        resolved_name[sizeof(resolved_name) - 1] = '\0';
        filename_length = (uint8_t) strlen(resolved_name);
        if (sscanf(parent_str, "%u", &parent_inode) != 1) {
            fprintf(stderr, "inserter: invalid parent inode '%s'\n", parent_str);
            fclose(fptr);
            exit(1);
        }
    }

    struct EXT2DriverRequest request;
    request.buf          = file_buffer;
    request.buffer_size  = (uint32_t) filesize;
    request.name         = resolved_name;
    request.name_len     = filename_length;
    request.is_directory = false;
    request.parent_inode = parent_inode;

    printf("Inserting '%s' (%lu bytes) into inode %u\n", resolved_name,
           (unsigned long) filesize, parent_inode);

    int retcode = write(&request);
    if (retcode == 1 && is_replace) {
        retcode = delete (request);
        retcode = write(&request);
    }
    if (retcode == 0)
        puts("Write success");
    else if (retcode == 1)
        puts("Error: File/folder name already exist");
    else if (retcode == 2)
        puts("Error: Invalid parent node index");
    else
        puts("Error: Unknown error");

    // Write image in memory into original, overwrite them
    fseek(fptr, 0, SEEK_SET);
    fwrite(image_storage, STORAGE_SIZE, 1, fptr);
    fclose(fptr);

    return 0;
}
