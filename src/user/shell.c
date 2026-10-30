/**
 * mbOSok shell: a ring-3 user program that only talks to the kernel through
 * the INT 0x30 syscall interface.
 *
 * Milestone 4 additions:
 *   - exec / ps / kill process management commands,
 *   - PATH environment variable with persistence (export / env),
 *   - external commands resolved through PATH (separate shell commands).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/user/syscall.h"

/* ------------------------------------------------------------------ */
/* System call interface                                              */
/* ------------------------------------------------------------------ */

static uint32_t syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t result;
    __asm__ volatile("int $0x30"
                     : "=a"(result)
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
    return result;
}

static int8_t syscall_read(struct EXT2DriverRequest *request) {
    int8_t retcode = -1;
    syscall(SYS_READ, (uint32_t) request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t syscall_read_dir(struct EXT2DriverRequest *request) {
    int8_t retcode = -1;
    syscall(SYS_READ_DIR, (uint32_t) request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t syscall_write(struct EXT2DriverRequest *request) {
    int8_t retcode = -1;
    syscall(SYS_WRITE, (uint32_t) request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t syscall_delete(struct EXT2DriverRequest request) {
    int8_t retcode = -1;
    syscall(SYS_DELETE, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

/* ------------------------------------------------------------------ */
/* Small libc                                                         */
/* ------------------------------------------------------------------ */

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static bool str_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (cap == 0) return;
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void str_append(char *dst, const char *src, uint32_t cap) {
    uint32_t len = str_len(dst);
    if (len >= cap) return;
    str_copy(dst + len, src, cap - len);
}

static char *str_chr(const char *s, char c) {
    while (*s != '\0') {
        if (*s == c) return (char *) s;
        s++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Output handling (buffer enables pipelines)                          */
/* ------------------------------------------------------------------ */

#define PIPE_BUFFER_SIZE 65536
#define DIR_BUFFER_SIZE  32768
#define FILE_BUFFER_SIZE 131072
#define LINE_MAX         512
#define MAX_SEGMENTS     8
#define MAX_ARGS         8
#define PATH_MAX         256
#define PATH_DIR_MAX     4

static char io_buffer[2][PIPE_BUFFER_SIZE];
static char *pipe_out;
static uint32_t pipe_out_len;
static char *pipe_in;
static uint32_t pipe_in_len;

static uint8_t dir_buffer[DIR_BUFFER_SIZE];
static uint8_t file_buffer[FILE_BUFFER_SIZE];
static char line_buffer[LINE_MAX];

static char cwd_path[PATH_MAX] = "/";
static uint32_t cwd_inode = 1; /* root inode in EXT2 IF2130 Edition */

static char path_dirs[PATH_DIR_MAX][PATH_MAX];
static uint32_t path_dir_count;

static void out_char(char c) {
    if (pipe_out_len < PIPE_BUFFER_SIZE) {
        pipe_out[pipe_out_len++] = c;
    }
}

static void out_str(const char *s) {
    while (*s != '\0') out_char(*s++);
}

static void out_newline(void) {
    out_char('\n');
}

static void print_error(const char *message, const char *detail) {
    out_str(message);
    if (detail != NULL) out_str(detail);
    out_newline();
}

/* ------------------------------------------------------------------ */
/* Directory helpers                                                  */
/* ------------------------------------------------------------------ */

static struct EXT2DirectoryEntry *entry_at(uint32_t offset) {
    return (struct EXT2DirectoryEntry *) (dir_buffer + offset);
}

static char *entry_name(struct EXT2DirectoryEntry *entry) {
    return (char *) (entry + 1);
}

static bool is_dot_entry(struct EXT2DirectoryEntry *entry) {
    char *name = entry_name(entry);
    if (entry->name_len == 1 && name[0] == '.') return true;
    if (entry->name_len == 2 && name[0] == '.' && name[1] == '.') return true;
    return false;
}

static int8_t load_directory(uint32_t inode) {
    struct EXT2DriverRequest request = {
        .buf          = dir_buffer,
        .name         = NULL,
        .name_len     = 0,
        .parent_inode = inode,
        .buffer_size  = DIR_BUFFER_SIZE,
        .is_directory = true,
    };
    memset(dir_buffer, 0, DIR_BUFFER_SIZE);
    return syscall_read_dir(&request);
}

/**
 * Look up one entry inside a directory. Returns 0 when not found.
 */
static uint32_t find_in_dir(const char *name, uint32_t parent_inode, uint8_t *type_out) {
    if (name == NULL) return 0;
    if (load_directory(parent_inode) != 0) return 0;

    uint8_t name_len = (uint8_t) str_len(name);
    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
        struct EXT2DirectoryEntry *entry = entry_at(offset);
        if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
        if (entry->inode != 0 && entry->name_len == name_len &&
            memcmp(entry_name(entry), name, name_len) == 0) {
            if (type_out != NULL) *type_out = entry->file_type;
            return entry->inode;
        }
        offset += entry->rec_len;
    }
    return 0;
}

static int8_t write_file(const char *name, uint32_t parent_inode, void *buf, uint32_t size) {
    struct EXT2DriverRequest request = {
        .buf          = buf,
        .name         = (char *) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size  = size,
        .is_directory = false,
    };
    return syscall_write(&request);
}

static int8_t make_directory(const char *name, uint32_t parent_inode) {
    struct EXT2DriverRequest request = {
        .buf          = NULL,
        .name         = (char *) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = true,
    };
    return syscall_write(&request);
}

static int8_t delete_entry(const char *name, uint32_t parent_inode, bool is_directory) {
    struct EXT2DriverRequest request = {
        .buf          = NULL,
        .name         = (char *) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = is_directory,
    };
    return syscall_delete(request);
}

static int8_t read_file(const char *name, uint32_t parent_inode, void *buf, uint32_t buf_size) {
    struct EXT2DriverRequest request = {
        .buf          = buf,
        .name         = (char *) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size  = buf_size,
        .is_directory = false,
    };
    return syscall_read(&request);
}

static int8_t query_file_size(const char *name, uint32_t parent_inode, uint32_t *size_out) {
    struct EXT2DriverRequest request = {
        .buf          = NULL,
        .name         = (char *) name,
        .name_len     = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size  = 0,
        .is_directory = false,
    };
    int8_t retcode = -1;
    syscall(SYS_FILE_SIZE, (uint32_t) &request, (uint32_t) size_out, (uint32_t) &retcode);
    return retcode;
}

/* ------------------------------------------------------------------ */
/* Path handling                                                      */
/* ------------------------------------------------------------------ */

static void path_push(const char *name) {
    if (str_eq(cwd_path, "/")) {
        str_copy(cwd_path + 1, name, PATH_MAX - 1);
    } else {
        str_append(cwd_path, "/", PATH_MAX);
        str_append(cwd_path, name, PATH_MAX);
    }
}

static void path_pop(void) {
    uint32_t len = str_len(cwd_path);
    if (len <= 1) {
        str_copy(cwd_path, "/", PATH_MAX);
        return;
    }
    while (len > 1 && cwd_path[len - 1] != '/') len--;
    if (len <= 1) {
        str_copy(cwd_path, "/", PATH_MAX);
    } else {
        cwd_path[len - 1] = '\0';
    }
}

/**
 * Resolve an intermediate path segment (which may be "." or "..") inside a
 * directory. Returns the child inode, or 0 when it is not a directory.
 */
static uint32_t dir_child_inode(const char *name, uint32_t name_len, uint32_t dir_inode) {
    if (name_len == 1 && name[0] == '.') return dir_inode;
    if (name_len == 2 && name[0] == '.' && name[1] == '.') {
        if (load_directory(dir_inode) != 0) return 0;
        uint32_t offset = 0;
        uint32_t index = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
            struct EXT2DirectoryEntry *entry = entry_at(offset);
            if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
            if (index == 1) return entry->inode;
            offset += entry->rec_len;
            index++;
        }
        return 0;
    }
    if (name_len >= PATH_MAX) return 0;
    char segment[PATH_MAX];
    memcpy(segment, name, name_len);
    segment[name_len] = '\0';
    uint8_t type = 0;
    uint32_t inode = find_in_dir(segment, dir_inode, &type);
    if (inode == 0 || type != EXT2_FT_DIR) return 0;
    return inode;
}

/**
 * Resolve a path into (parent directory inode, final component).
 * Returns true on success. Absolute paths start at the root, relative paths
 * at the current working directory.
 */
static bool resolve_parent(
    const char *path,
    uint32_t *parent_inode_out,
    const char **name_out,
    uint8_t *name_len_out
) {
    if (path == NULL || *path == '\0') return false;

    uint32_t inode = 1;
    const char *cursor = path;
    if (*cursor == '/') cursor++;
    else inode = cwd_inode;
    if (*cursor == '\0') return false;

    while (true) {
        const char *slash = cursor;
        while (*slash != '\0' && *slash != '/') slash++;
        uint32_t segment_len = (uint32_t) (slash - cursor);
        if (segment_len == 0 || segment_len >= PATH_MAX) return false;

        if (*slash == '\0') {
            *parent_inode_out = inode;
            *name_out = cursor;
            *name_len_out = (uint8_t) segment_len;
            return true;
        }

        inode = dir_child_inode(cursor, segment_len, inode);
        if (inode == 0) return false;
        cursor = slash + 1;
        if (*cursor == '\0') return false;
    }
}

/**
 * Check whether an absolute (or cwd-relative) path points to an existing file.
 */
static bool file_exists(const char *path) {
    uint32_t parent_inode = 0;
    const char *name = NULL;
    uint8_t name_len = 0;
    if (!resolve_parent(path, &parent_inode, &name, &name_len)) return false;

    char filename[PATH_MAX];
    if (name_len == 0) return false;
    memcpy(filename, name, name_len);
    filename[name_len] = '\0';
    uint32_t size = 0;
    return query_file_size(filename, parent_inode, &size) == 0;
}

/**
 * Build an absolute path from cwd + path (path may already be absolute).
 */
static void make_absolute(const char *path, char *out, uint32_t cap) {
    if (path[0] == '/') {
        str_copy(out, path, cap);
        return;
    }
    if (str_eq(cwd_path, "/")) {
        str_copy(out, "/", cap);
        str_append(out, path, cap);
    } else {
        str_copy(out, cwd_path, cap);
        str_append(out, "/", cap);
        str_append(out, path, cap);
    }
}

/**
 * Resolve a command name to a concrete executable path, trying the path
 * as-is and then every PATH entry. Returns true and fills out when found.
 */
static bool resolve_executable(const char *name, char *out, uint32_t cap) {
    char candidate[PATH_MAX];
    if (name[0] == '/' || str_chr(name, '/') != NULL) {
        make_absolute(name, candidate, PATH_MAX);
        if (file_exists(candidate)) {
            str_copy(out, candidate, cap);
            return true;
        }
        return false;
    }

    make_absolute(name, candidate, PATH_MAX);
    if (file_exists(candidate)) {
        str_copy(out, candidate, cap);
        return true;
    }

    for (uint32_t i = 0; i < path_dir_count; i++) {
        str_copy(candidate, path_dirs[i], PATH_MAX);
        if (!str_eq(candidate, "/")) str_append(candidate, "/", PATH_MAX);
        str_append(candidate, name, PATH_MAX);
        if (candidate[0] != '/') {
            char absolute_candidate[PATH_MAX];
            make_absolute(candidate, absolute_candidate, PATH_MAX);
            str_copy(candidate, absolute_candidate, PATH_MAX);
        }
        if (file_exists(candidate)) {
            str_copy(out, candidate, cap);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Environment (PATH)                                                 */
/* ------------------------------------------------------------------ */

#define PATH_CONFIG_NAME "path.cfg"

static void path_set_default(void) {
    path_dir_count = 1;
    str_copy(path_dirs[0], "/bin", PATH_MAX);
}

/**
 * Copy one PATH entry, making sure it is an absolute path.
 */
static void path_add_entry(uint32_t index, const char *value, uint32_t length) {
    if (length == 0 || length >= PATH_MAX - 1) return;
    uint32_t offset = 0;
    if (value[0] != '/') {
        path_dirs[index][0] = '/';
        offset = 1;
    }
    for (uint32_t i = 0; i < length && offset + i < PATH_MAX - 1; i++) {
        path_dirs[index][offset + i] = value[i];
    }
    path_dirs[index][offset + length] = '\0';
}

static void path_load(void) {
    path_set_default();
    uint32_t size = 0;
    if (query_file_size(PATH_CONFIG_NAME, 1, &size) != 0 || size == 0 || size >= FILE_BUFFER_SIZE)
        return;
    if (read_file(PATH_CONFIG_NAME, 1, file_buffer, FILE_BUFFER_SIZE) != 0) return;

    file_buffer[size] = '\0';
    char *cursor = (char *) file_buffer;
    path_dir_count = 0;
    while (*cursor != '\0' && path_dir_count < PATH_DIR_MAX) {
        char *separator = str_chr(cursor, ':');
        uint32_t length = (separator != NULL) ? (uint32_t) (separator - cursor) : str_len(cursor);
        if (length > 0 && length < PATH_MAX) {
            path_add_entry(path_dir_count, cursor, length);
            path_dir_count++;
        }
        if (separator == NULL) break;
        cursor = separator + 1;
    }
    if (path_dir_count == 0) path_set_default();
}

static void path_save(void) {
    char config[PATH_DIR_MAX * PATH_MAX];
    config[0] = '\0';
    for (uint32_t i = 0; i < path_dir_count; i++) {
        if (i > 0) str_append(config, ":", sizeof(config));
        str_append(config, path_dirs[i], sizeof(config));
    }

    uint32_t existing_size = 0;
    if (query_file_size(PATH_CONFIG_NAME, 1, &existing_size) == 0) {
        if (delete_entry(PATH_CONFIG_NAME, 1, false) != 0) return;
    }
    uint32_t size = str_len(config);
    if (size > 0) (void) write_file(PATH_CONFIG_NAME, 1, config, size);
}

static void print_path(void) {
    out_str("PATH=");
    for (uint32_t i = 0; i < path_dir_count; i++) {
        if (i > 0) out_char(':');
        out_str(path_dirs[i]);
    }
    out_newline();
}

static void path_set(const char *value) {
    char config[LINE_MAX];
    str_copy(config, value, sizeof(config));

    uint32_t count = 0;
    char *cursor = config;
    while (*cursor != '\0' && count < PATH_DIR_MAX) {
        char *separator = str_chr(cursor, ':');
        uint32_t length = (separator != NULL) ? (uint32_t) (separator - cursor) : str_len(cursor);
        if (length > 0 && length < PATH_MAX) {
            path_add_entry(count, cursor, length);
            count++;
        }
        if (separator == NULL) break;
        cursor = separator + 1;
    }
    if (count == 0) {
        print_error("export: invalid PATH", NULL);
        return;
    }
    path_dir_count = count;
    path_save();
    print_path();
}

/* ------------------------------------------------------------------ */
/* Recursive copy / remove (folder bonus, also used by mv)            */
/* ------------------------------------------------------------------ */

#define TREE_MAX_CHILDREN 48
#define TREE_MAX_DEPTH    6

struct child_info {
    uint32_t inode;
    uint8_t  file_type;
    uint8_t  name_len;
    char     name[64];
};

static int8_t collect_children(struct child_info *children, uint32_t *count_out) {
    uint32_t count = 0;
    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE && count < TREE_MAX_CHILDREN) {
        struct EXT2DirectoryEntry *entry = entry_at(offset);
        if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
        if (entry->inode != 0 && !is_dot_entry(entry) && entry->name_len < 64) {
            children[count].inode = entry->inode;
            children[count].file_type = entry->file_type;
            children[count].name_len = (uint8_t) entry->name_len;
            memcpy(children[count].name, entry_name(entry), entry->name_len);
            children[count].name[entry->name_len] = '\0';
            count++;
        }
        offset += entry->rec_len;
    }
    *count_out = count;
    return 0;
}

static int8_t copy_tree(uint32_t src_inode, uint32_t dst_parent, const char *dst_name, uint32_t depth) {
    if (depth > TREE_MAX_DEPTH) return -1;
    if (load_directory(src_inode) != 0) return -1;

    struct child_info children[TREE_MAX_CHILDREN];
    uint32_t child_count = 0;
    collect_children(children, &child_count);

    if (make_directory(dst_name, dst_parent) != 0) return -1;
    uint32_t dst_inode = find_in_dir(dst_name, dst_parent, NULL);
    if (dst_inode == 0) return -1;

    for (uint32_t i = 0; i < child_count; i++) {
        if (children[i].file_type == EXT2_FT_DIR) {
            if (copy_tree(children[i].inode, dst_inode, children[i].name, depth + 1) != 0)
                return -1;
        } else {
            uint32_t size = 0;
            if (query_file_size(children[i].name, src_inode, &size) != 0) return -1;
            if (size == 0 || size > FILE_BUFFER_SIZE) return -1;
            if (read_file(children[i].name, src_inode, file_buffer, FILE_BUFFER_SIZE) != 0)
                return -1;
            if (write_file(children[i].name, dst_inode, file_buffer, size) != 0)
                return -1;
        }
    }
    return 0;
}

static int8_t remove_tree(uint32_t inode, uint32_t parent_inode, const char *name, uint8_t file_type) {
    if (file_type != EXT2_FT_DIR) {
        return delete_entry(name, parent_inode, false);
    }
    if (load_directory(inode) != 0) return -1;

    struct child_info children[TREE_MAX_CHILDREN];
    uint32_t child_count = 0;
    collect_children(children, &child_count);

    for (uint32_t i = 0; i < child_count; i++) {
        if (remove_tree(children[i].inode, inode, children[i].name, children[i].file_type) != 0)
            return -1;
    }
    return delete_entry(name, parent_inode, true);
}

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */

static void cmd_help(void) {
    out_str("Available commands:\n");
    out_str("  help              show this help\n");
    out_str("  clear             clear the screen\n");
    out_str("  pwd               print working directory\n");
    out_str("  cd [dir|..|/]     change working directory\n");
    out_str("  ls                list current directory\n");
    out_str("  mkdir <name>      create a new directory\n");
    out_str("  cat <file>        print a file\n");
    out_str("  cp <src> <dst>    copy file or folder\n");
    out_str("  rm <name>         remove file or folder\n");
    out_str("  mv <src> <dst>    move / rename file or folder\n");
    out_str("  find <name>       search the whole filesystem\n");
    out_str("  grep <pat> [f]    search a pattern in a file or pipeline\n");
    out_str("\nProcess management:\n");
    out_str("  exec <name> [a..] run an executable (PATH aware, optional &)\n");
    out_str("  ps                list processes\n");
    out_str("  kill <pid>        terminate a process\n");
    out_str("  env               print environment variables\n");
    out_str("  export PATH <v>   set PATH (persisted to /path.cfg)\n");
    out_str("\nExternal commands found through PATH are also executable by name,\n");
    out_str("e.g. hello, clock, echo. Pipelines are supported, e.g. ls | grep txt\n");
}

static void cmd_ls(void) {
    if (load_directory(cwd_inode) != 0) {
        print_error("ls: cannot read directory", NULL);
        return;
    }
    bool first = true;
    uint32_t offset = 0;
    while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
        struct EXT2DirectoryEntry *entry = entry_at(offset);
        if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
        if (entry->inode != 0 && !is_dot_entry(entry)) {
            if (!first) out_char(' ');
            first = false;
            for (uint32_t i = 0; i < entry->name_len; i++) out_char(entry_name(entry)[i]);
            if (entry->file_type == EXT2_FT_DIR) out_char('/');
        }
        offset += entry->rec_len;
    }
    out_newline();
}

static void cmd_cd(const char *dest) {
    if (dest == NULL || str_eq(dest, ".")) return;
    if (str_eq(dest, "/")) {
        cwd_inode = 1;
        str_copy(cwd_path, "/", PATH_MAX);
        return;
    }
    if (str_eq(dest, "..")) {
        if (load_directory(cwd_inode) != 0) {
            print_error("cd: cannot read directory", NULL);
            return;
        }
        uint32_t offset = 0;
        uint32_t index = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
            struct EXT2DirectoryEntry *entry = entry_at(offset);
            if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
            if (index == 1) {
                cwd_inode = entry->inode;
                break;
            }
            offset += entry->rec_len;
            index++;
        }
        path_pop();
        return;
    }

    uint8_t type = 0;
    uint32_t inode = find_in_dir(dest, cwd_inode, &type);
    if (inode == 0 || type != EXT2_FT_DIR) {
        print_error("cd: not a directory: ", dest);
        return;
    }
    cwd_inode = inode;
    path_push(dest);
}

static void cmd_mkdir(const char *name) {
    if (name == NULL) {
        print_error("mkdir: missing operand", NULL);
        return;
    }
    int8_t retcode = make_directory(name, cwd_inode);
    if (retcode == 1) print_error("mkdir: already exists: ", name);
    else if (retcode != 0) print_error("mkdir: failed: ", name);
}

static void cmd_cat(const char *name) {
    if (name == NULL) {
        print_error("cat: missing operand", NULL);
        return;
    }
    uint32_t size = 0;
    int8_t retcode = query_file_size(name, cwd_inode, &size);
    if (retcode != 0) {
        print_error("cat: cannot open: ", name);
        return;
    }
    if (size > FILE_BUFFER_SIZE) {
        print_error("cat: file too large: ", name);
        return;
    }
    if (read_file(name, cwd_inode, file_buffer, FILE_BUFFER_SIZE) != 0) {
        print_error("cat: read failed: ", name);
        return;
    }
    for (uint32_t i = 0; i < size; i++) out_char((char) file_buffer[i]);
    if (size > 0 && file_buffer[size - 1] != '\n') out_newline();
}

static void cmd_cp(const char *src, const char *dst) {
    if (src == NULL || dst == NULL) {
        print_error("cp: usage: cp <src> <dst>", NULL);
        return;
    }
    uint8_t type = 0;
    uint32_t inode = find_in_dir(src, cwd_inode, &type);
    if (inode == 0) {
        print_error("cp: not found: ", src);
        return;
    }
    if (find_in_dir(dst, cwd_inode, NULL) != 0) {
        print_error("cp: destination exists: ", dst);
        return;
    }
    if (type == EXT2_FT_DIR) {
        if (copy_tree(inode, cwd_inode, dst, 0) != 0)
            print_error("cp: failed to copy folder: ", src);
        return;
    }
    uint32_t size = 0;
    if (query_file_size(src, cwd_inode, &size) != 0 || size > FILE_BUFFER_SIZE || size == 0) {
        print_error("cp: cannot read: ", src);
        return;
    }
    if (read_file(src, cwd_inode, file_buffer, FILE_BUFFER_SIZE) != 0) {
        print_error("cp: read failed: ", src);
        return;
    }
    if (write_file(dst, cwd_inode, file_buffer, size) != 0)
        print_error("cp: write failed: ", dst);
}

static void cmd_rm(const char *name) {
    if (name == NULL) {
        print_error("rm: missing operand", NULL);
        return;
    }
    uint8_t type = 0;
    uint32_t inode = find_in_dir(name, cwd_inode, &type);
    if (inode == 0) {
        print_error("rm: not found: ", name);
        return;
    }
    if (remove_tree(inode, cwd_inode, name, type) != 0)
        print_error("rm: failed: ", name);
}

static void cmd_mv(const char *src, const char *dst) {
    if (src == NULL || dst == NULL) {
        print_error("mv: usage: mv <src> <dst>", NULL);
        return;
    }
    uint8_t type = 0;
    uint32_t inode = find_in_dir(src, cwd_inode, &type);
    if (inode == 0) {
        print_error("mv: not found: ", src);
        return;
    }
    if (find_in_dir(dst, cwd_inode, NULL) != 0) {
        print_error("mv: destination exists: ", dst);
        return;
    }
    if (type == EXT2_FT_DIR) {
        if (copy_tree(inode, cwd_inode, dst, 0) != 0 || remove_tree(inode, cwd_inode, src, type) != 0)
            print_error("mv: failed: ", src);
        return;
    }
    uint32_t size = 0;
    if (query_file_size(src, cwd_inode, &size) != 0 || size > FILE_BUFFER_SIZE || size == 0) {
        print_error("mv: cannot read: ", src);
        return;
    }
    if (read_file(src, cwd_inode, file_buffer, FILE_BUFFER_SIZE) != 0) {
        print_error("mv: read failed: ", src);
        return;
    }
    if (write_file(dst, cwd_inode, file_buffer, size) != 0) {
        print_error("mv: write failed: ", dst);
        return;
    }
    if (delete_entry(src, cwd_inode, false) != 0)
        print_error("mv: could not remove source: ", src);
}

struct find_frame {
    uint32_t inode;
    uint32_t path_len;
    char     path[200];
};

#define FIND_STACK_MAX 64

static void cmd_find(const char *target) {
    if (target == NULL) {
        print_error("find: missing operand", NULL);
        return;
    }
    uint8_t target_len = (uint8_t) str_len(target);
    static struct find_frame stack[FIND_STACK_MAX];
    uint32_t stack_size = 0;

    stack[stack_size].inode = 1;
    stack[stack_size].path[0] = '/';
    stack[stack_size].path[1] = '\0';
    stack[stack_size].path_len = 1;
    stack_size++;

    while (stack_size > 0) {
        stack_size--;
        uint32_t dir_inode = stack[stack_size].inode;
        uint32_t base_len = stack[stack_size].path_len;
        char path[200];
        memcpy(path, stack[stack_size].path, base_len + 1);

        if (load_directory(dir_inode) != 0) continue;

        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
            struct EXT2DirectoryEntry *entry = entry_at(offset);
            if (entry->rec_len < sizeof(struct EXT2DirectoryEntry)) break;
            if (entry->inode != 0 && !is_dot_entry(entry)) {
                if (entry->name_len == target_len &&
                    memcmp(entry_name(entry), target, target_len) == 0) {
                    out_str(path);
                    if (!str_eq(path, "/")) out_char('/');
                    for (uint32_t i = 0; i < entry->name_len; i++) out_char(entry_name(entry)[i]);
                    if (entry->file_type == EXT2_FT_DIR) out_char('/');
                    out_newline();
                }
                if (entry->file_type == EXT2_FT_DIR && stack_size < FIND_STACK_MAX &&
                    base_len + entry->name_len + 2 < 200) {
                    struct find_frame *frame = &stack[stack_size];
                    memcpy(frame->path, path, base_len);
                    uint32_t len = base_len;
                    if (len > 1) frame->path[len++] = '/';
                    memcpy(frame->path + len, entry_name(entry), entry->name_len);
                    len += entry->name_len;
                    frame->path[len] = '\0';
                    frame->path_len = len;
                    frame->inode = entry->inode;
                    stack_size++;
                }
            }
            offset += entry->rec_len;
        }
    }
}

static bool line_contains(const char *line, uint32_t len, const char *pattern) {
    uint32_t pattern_len = str_len(pattern);
    if (pattern_len == 0) return true;
    if (pattern_len > len) return false;
    for (uint32_t i = 0; i + pattern_len <= len; i++) {
        if (memcmp(line + i, pattern, pattern_len) == 0) return true;
    }
    return false;
}

static void cmd_grep(const char *pattern, const char *file) {
    if (pattern == NULL) {
        print_error("grep: missing pattern", NULL);
        return;
    }
    const char *data;
    uint32_t len;

    if (file != NULL) {
        uint32_t size = 0;
        if (query_file_size(file, cwd_inode, &size) != 0 || size > FILE_BUFFER_SIZE) {
            print_error("grep: cannot open: ", file);
            return;
        }
        if (read_file(file, cwd_inode, file_buffer, FILE_BUFFER_SIZE) != 0) {
            print_error("grep: read failed: ", file);
            return;
        }
        data = (const char *) file_buffer;
        len = size;
    } else {
        if (pipe_in == NULL || pipe_in_len == 0) {
            print_error("grep: no input", NULL);
            return;
        }
        data = pipe_in;
        len = pipe_in_len;
    }

    uint32_t start = 0;
    while (start < len) {
        uint32_t end = start;
        while (end < len && data[end] != '\n') end++;
        if (line_contains(data + start, end - start, pattern)) {
            for (uint32_t i = start; i < end; i++) out_char(data[i]);
            out_newline();
        }
        start = end + 1;
    }
}

/* ------------------------------------------------------------------ */
/* Process management commands                                        */
/* ------------------------------------------------------------------ */

static void cmd_exec(char *command_line_argv[], uint32_t argc) {
    if (argc < 2) {
        print_error("exec: usage: exec <file> [args...] [&]", NULL);
        return;
    }

    bool background = false;
    if (str_eq(command_line_argv[argc - 1], "&")) {
        background = true;
        argc--;
        if (argc < 2) {
            print_error("exec: missing file operand", NULL);
            return;
        }
    }

    char absolute[PATH_MAX];
    if (!resolve_executable(command_line_argv[1], absolute, PATH_MAX)) {
        print_error("exec: not found: ", command_line_argv[1]);
        return;
    }

    int32_t pid = (int32_t) syscall(
        SYS_EXEC,
        (uint32_t) absolute,
        (uint32_t) &command_line_argv[1],
        background ? 1u : 0u
    );
    if (pid < 0) {
        print_error("exec: failed to start: ", command_line_argv[1]);
        return;
    }

    out_str("[");
    char pid_buffer[12];
    uint32_t digits = 0;
    uint32_t value = (uint32_t) pid;
    if (value == 0) pid_buffer[digits++] = '0';
    while (value > 0) {
        pid_buffer[digits++] = (char) ('0' + value % 10);
        value /= 10;
    }
    while (digits > 0) out_char(pid_buffer[--digits]);
    out_str("] started ");
    out_str(command_line_argv[1]);
    if (background) out_str(" (background)");
    out_newline();
}

static void cmd_ps(void) {
    static char ps_buffer[4096];
    memset(ps_buffer, 0, sizeof(ps_buffer));
    int32_t count = (int32_t) syscall(SYS_PS, (uint32_t) ps_buffer, sizeof(ps_buffer) - 1, 0);
    if (count < 0) {
        print_error("ps: failed", NULL);
        return;
    }
    out_str(ps_buffer);
    out_str("total ");
    char num[12];
    uint32_t digits = 0;
    if (count == 0) num[digits++] = '0';
    while (count > 0) {
        num[digits++] = (char) ('0' + count % 10);
        count /= 10;
    }
    while (digits > 0) out_char(num[--digits]);
    out_str(" process(es)\n");
}

static void cmd_kill(const char *pid_text) {
    if (pid_text == NULL) {
        print_error("kill: usage: kill <pid>", NULL);
        return;
    }
    uint32_t pid = 0;
    for (uint32_t i = 0; pid_text[i] != '\0'; i++) {
        if (pid_text[i] < '0' || pid_text[i] > '9') {
            print_error("kill: invalid pid: ", pid_text);
            return;
        }
        pid = pid * 10 + (uint32_t) (pid_text[i] - '0');
    }
    if (pid == 0) {
        print_error("kill: invalid pid: ", pid_text);
        return;
    }
    int32_t result = (int32_t) syscall(SYS_KILL, pid, 0, 0);
    if (result == 1) {
        out_str("killed pid ");
        out_str(pid_text);
        out_newline();
    } else {
        print_error("kill: no such process: ", pid_text);
    }
}

/* ------------------------------------------------------------------ */
/* REPL                                                               */
/* ------------------------------------------------------------------ */

static bool is_builtin(const char *cmd) {
    static const char *builtins[] = {
        "help", "clear", "pwd", "cd", "ls", "mkdir", "cat", "cp", "rm", "mv",
        "find", "grep", "exec", "ps", "kill", "env", "export",
    };
    for (uint32_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (str_eq(cmd, builtins[i])) return true;
    }
    return false;
}

static void run_command(char *command) {
    char *argv[MAX_ARGS];
    uint32_t argc = 0;
    char *p = command;
    while (*p != '\0' && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
    if (argc == 0) return;
    if (argc < MAX_ARGS) argv[argc] = NULL;

    const char *cmd = argv[0];
    if (str_eq(cmd, "help")) {
        cmd_help();
    } else if (str_eq(cmd, "clear")) {
        syscall(SYS_CLEAR_SCREEN, 0, 0, 0);
    } else if (str_eq(cmd, "pwd")) {
        out_str(cwd_path);
        out_newline();
    } else if (str_eq(cmd, "ls")) {
        cmd_ls();
    } else if (str_eq(cmd, "cd")) {
        cmd_cd(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "mkdir")) {
        cmd_mkdir(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "cat")) {
        cmd_cat(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "cp")) {
        cmd_cp(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
    } else if (str_eq(cmd, "rm")) {
        cmd_rm(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "mv")) {
        cmd_mv(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
    } else if (str_eq(cmd, "find")) {
        cmd_find(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "grep")) {
        cmd_grep(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
    } else if (str_eq(cmd, "exec")) {
        cmd_exec(argv, argc);
    } else if (str_eq(cmd, "ps")) {
        cmd_ps();
    } else if (str_eq(cmd, "kill")) {
        cmd_kill(argc > 1 ? argv[1] : NULL);
    } else if (str_eq(cmd, "env")) {
        print_path();
    } else if (str_eq(cmd, "export")) {
        if (argc >= 3 && (str_eq(argv[1], "PATH") || str_eq(argv[1], "path"))) {
            path_set(argv[2]);
        } else if (argc == 2) {
            path_set(argv[1]);
        } else {
            print_error("export: usage: export [PATH] <dir[:dir...]>", NULL);
        }
    } else if (!is_builtin(cmd)) {
        /* Separate shell command: resolve through PATH and exec it. */
        char absolute[PATH_MAX];
        if (resolve_executable(cmd, absolute, PATH_MAX)) {
            int32_t pid = (int32_t) syscall(SYS_EXEC, (uint32_t) absolute, (uint32_t) argv, 0u);
            if (pid < 0) print_error(cmd, ": failed to execute");
        } else {
            print_error(cmd, ": command not found (try 'help')");
        }
    } else {
        print_error(cmd, ": command not implemented");
    }
}

static void run_line(char *line) {
    char *segments[MAX_SEGMENTS];
    uint32_t segment_count = 0;
    char *p = line;
    while (segment_count < MAX_SEGMENTS) {
        segments[segment_count++] = p;
        char *bar = str_chr(p, '|');
        if (bar == NULL) break;
        *bar = '\0';
        p = bar + 1;
    }

    pipe_in = NULL;
    pipe_in_len = 0;
    for (uint32_t i = 0; i < segment_count; i++) {
        pipe_out = io_buffer[(i + 1) & 1];
        pipe_out_len = 0;
        run_command(segments[i]);
        pipe_in = pipe_out;
        pipe_in_len = pipe_out_len;
    }

    if (pipe_in_len > 0) {
        syscall(SYS_PUTS, (uint32_t) pipe_in, pipe_in_len, 0xF);
    }
}

static void print_prompt(void) {
    syscall(SYS_PUTS, (uint32_t) "mbOSok", 6, 0x0A);
    syscall(SYS_PUTS, (uint32_t) cwd_path, str_len(cwd_path), 0x0B);
    syscall(SYS_PUTS, (uint32_t) "$ ", 2, 0xF);
}

static uint32_t read_line(void) {
    uint32_t len = 0;
    while (true) {
        char c = 0;
        while (c == '\0') {
            syscall(SYS_GETCHAR, (uint32_t) &c, 0, 0);
        }
        if (c == '\n' || c == '\r') {
            syscall(SYS_PUTCHAR, (uint32_t) '\n', 0xF, 0);
            line_buffer[len] = '\0';
            return len;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                syscall(SYS_PUTCHAR, (uint32_t) '\b', 0xF, 0);
            }
            continue;
        }
        if (len + 1 < LINE_MAX) {
            line_buffer[len++] = c;
            syscall(SYS_PUTCHAR, (uint32_t) (uint8_t) c, 0xF, 0);
        }
    }
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    syscall(SYS_ACTIVATE_KB, 0, 0, 0);
    syscall(SYS_CLEAR_SCREEN, 0, 0, 0);

    syscall(SYS_PUTS, (uint32_t) "mbOSok shell (Milestone 4)\n", 27, 0xA);
    syscall(SYS_PUTS, (uint32_t) "Type 'help' for a list of commands.\n\n", 37, 0xF);

    path_load();

    while (true) {
        print_prompt();
        read_line();
        if (str_len(line_buffer) == 0) continue;
        run_line(line_buffer);
    }
}
