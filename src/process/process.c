#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

struct ProcessManagerState process_manager_state = {
    .active_process_count  = 0,
    .current_process_index = PROCESS_COUNT_MAX,
    .next_pid              = 1,
};

#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_PAGE_COUNT * PAGE_FRAME_SIZE)
#define ARG_COUNT_MAX     16
#define ARG_STRING_MAX    128
#define NAME_LENGTH_MAX   256

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static uint32_t process_generate_new_pid(void) {
    return process_manager_state.next_pid++;
}

static int32_t process_list_get_inactive_index(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_UNUSED) return (int32_t) i;
    }
    return -1;
}

/**
 * Write argc, argv strings and the argv pointer vector onto the top of the
 * new process' user stack, then set the saved user esp accordingly.
 *
 * Standard cdecl main(argc, argv) layout at `context->cpu.stack.esp`:
 *   [esp]              = argc
 *   [esp+4]            = pointer to argv[0]
 *   [esp+8]            = argv[0]
 *   ...
 *   [esp+8+4*argc]     = NULL
 *
 * The caller guarantees argv strings live in kernel memory (mapped in every
 * address space), because this runs with the new page directory active.
 */
static bool build_user_stack(struct Context *context, int argc, const char *const argv[]) {
    if (argc <= 0 || argc > ARG_COUNT_MAX) return false;

    uint32_t sp = USER_STACK_TOP;
    uint32_t arg_addr[ARG_COUNT_MAX];

    for (int32_t i = argc - 1; i >= 0; i--) {
        const char *arg = argv[i];
        uint32_t len = 0;
        while (len <= ARG_STRING_MAX && arg[len] != '\0') len++;
        if (len > ARG_STRING_MAX) return false;

        sp -= len + 1;
        memcpy((void *) sp, arg, len + 1);
        arg_addr[i] = sp;
        if (sp < USER_STACK_BOTTOM + 64) return false;
    }

    sp &= ~3u;
    sp -= 4;
    *((uint32_t *) sp) = 0;
    for (int32_t i = argc - 1; i >= 0; i--) {
        sp -= 4;
        *((uint32_t *) sp) = arg_addr[i];
    }

    /* argv points to the pointer vector just built */
    uint32_t argv_pointer = sp;
    sp -= 4;
    *((uint32_t *) sp) = argv_pointer;
    sp -= 4;
    *((uint32_t *) sp) = (uint32_t) argc;

    if (sp < USER_STACK_BOTTOM) return false;
    context->cpu.stack.esp = sp;
    return true;
}

static int32_t process_create_internal(
    struct EXT2DriverRequest request,
    int argc,
    const char *const argv[]
) {
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    }

    if (request.name == NULL || request.name_len == 0 || request.parent_inode == 0) {
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    }
    if (argc <= 0 || argc > ARG_COUNT_MAX) {
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    }

    /* Snapshot the executable name and argv strings into kernel stack
     * memory. After switching CR3 to the new page directory, pointers into
     * the caller's address space (e.g. the shell) would no longer resolve. */
    char name_buffer[NAME_LENGTH_MAX];
    uint32_t name_length = request.name_len;
    if (name_length >= NAME_LENGTH_MAX) name_length = NAME_LENGTH_MAX - 1;
    for (uint32_t i = 0; i < name_length; i++) name_buffer[i] = request.name[i];
    name_buffer[name_length] = '\0';
    request.name = name_buffer;
    request.name_len = (uint8_t) name_length;

    char arg_buffers[ARG_COUNT_MAX][ARG_STRING_MAX + 1];
    const char *safe_argv[ARG_COUNT_MAX];
    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        uint32_t length = 0;
        while (length <= ARG_STRING_MAX && argv[i][length] != '\0') length++;
        if (length > ARG_STRING_MAX) return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        for (uint32_t j = 0; j <= length; j++) arg_buffers[i][j] = argv[i][j];
        safe_argv[i] = arg_buffers[i];
    }

    /* Trust the EXT2 inode, not request.buffer_size, so callers do not need
     * to know the executable size in advance. */
    uint32_t file_size = 0;
    if (get_file_size(request.name, request.name_len, request.parent_inode, &file_size) != 0
        || file_size == 0) {
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    }

    uint32_t code_frame_count = ceil_div(file_size, PAGE_FRAME_SIZE);
    uint32_t page_frame_count = code_frame_count + USER_STACK_PAGE_COUNT;
    if (page_frame_count > PROCESS_PAGE_FRAME_COUNT_MAX
        || !paging_allocate_check(page_frame_count * PAGE_FRAME_SIZE)) {
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }

    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    struct ProcessControlBlock *new_pcb = &_process_list[p_index];

    struct PageDirectory *new_page_directory = paging_create_new_page_directory();
    if (new_page_directory == NULL) return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    struct PageDirectory *old_page_directory = paging_get_current_page_directory_addr();
    paging_use_page_directory(new_page_directory);

    int32_t retcode = PROCESS_CREATE_SUCCESS;
    uint32_t mapped_frame_count = 0;

    for (uint32_t i = 0; i < code_frame_count; i++) {
        void *virtual_addr = (void *) (i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_page_directory, virtual_addr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[mapped_frame_count++] = virtual_addr;
    }

    for (uint32_t i = 0; i < USER_STACK_PAGE_COUNT; i++) {
        void *virtual_addr = (void *) (USER_STACK_BOTTOM + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_page_directory, virtual_addr)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[mapped_frame_count++] = virtual_addr;
    }

    /* Load the executable into the new address space (the kernel is still
     * mapped through PDE[0x300], so syscall/ISR code keeps running). */
    request.buf = (void *) 0;
    request.buffer_size = file_size;
    request.is_directory = false;
    if (read(request) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    memset(&new_pcb->context, 0, sizeof(new_pcb->context));
    new_pcb->context.cpu.segment.ds = 0x20 | 0x3;
    new_pcb->context.cpu.segment.es = 0x20 | 0x3;
    new_pcb->context.cpu.segment.fs = 0x20 | 0x3;
    new_pcb->context.cpu.segment.gs = 0x20 | 0x3;
    new_pcb->context.eip = 0;
    new_pcb->context.eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = new_page_directory;

    if (!build_user_stack(&new_pcb->context, argc, safe_argv)) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    /* Restore the virtual address space used before process creation. */
    paging_use_page_directory(old_page_directory);

    new_pcb->metadata.pid = process_generate_new_pid();
    uint32_t copy_length = request.name_len;
    if (copy_length >= PROCESS_NAME_LENGTH_MAX) copy_length = PROCESS_NAME_LENGTH_MAX - 1;
    for (uint32_t i = 0; i < copy_length; i++) new_pcb->metadata.name[i] = request.name[i];
    new_pcb->metadata.name[copy_length] = '\0';
    new_pcb->metadata.state = PROCESS_STATE_READY;
    new_pcb->memory.page_frame_used_count = mapped_frame_count;
    process_manager_state.active_process_count++;
    return PROCESS_CREATE_SUCCESS;

exit_cleanup:
    for (uint32_t i = 0; i < mapped_frame_count; i++) {
        paging_free_user_page_frame(new_page_directory, new_pcb->memory.virtual_addr_used[i]);
    }
    paging_use_page_directory(old_page_directory);
    paging_free_page_directory(new_page_directory);
    memset(new_pcb, 0, sizeof(*new_pcb));
    return retcode;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    const char *argv[1];
    char name_buffer[NAME_LENGTH_MAX];
    uint32_t length = request.name_len;
    if (length >= NAME_LENGTH_MAX) length = NAME_LENGTH_MAX - 1;
    for (uint32_t i = 0; i < length; i++) name_buffer[i] = request.name[i];
    name_buffer[length] = '\0';
    argv[0] = name_buffer;
    return process_create_internal(request, 1, argv);
}

int32_t process_create_user_process_args(
    struct EXT2DriverRequest request,
    int argc,
    const char *const argv[]
) {
    return process_create_internal(request, argc, argv);
}

struct ProcessControlBlock *process_get_current_running_pcb_pointer(void) {
    if (process_manager_state.current_process_index >= PROCESS_COUNT_MAX) return NULL;
    struct ProcessControlBlock *pcb =
        &_process_list[process_manager_state.current_process_index];
    if (pcb->metadata.state == PROCESS_STATE_UNUSED) return NULL;
    return pcb;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state == PROCESS_STATE_UNUSED) continue;
        if (pcb->metadata.pid != pid) continue;

        struct PageDirectory *page_directory = pcb->context.page_directory_virtual_addr;
        if (page_directory == NULL) return false;

        bool is_current = (i == process_manager_state.current_process_index);
        struct PageDirectory *old_page_directory = paging_get_current_page_directory_addr();

        /* paging_free_user_page_frame() only works on the active CR3, so
         * temporarily activate the target address space when needed. */
        if (!is_current) {
            paging_use_page_directory(page_directory);
        }

        for (uint32_t j = 0; j < pcb->memory.page_frame_used_count; j++) {
            if (pcb->memory.virtual_addr_used[j] != NULL) {
                paging_free_user_page_frame(page_directory, pcb->memory.virtual_addr_used[j]);
            }
        }

        if (is_current) {
            /* Never free a page directory that is still loaded in CR3. */
            paging_use_page_directory(&_paging_kernel_page_directory);
            process_manager_state.current_process_index = PROCESS_COUNT_MAX;
        } else {
            paging_use_page_directory(old_page_directory);
        }

        paging_free_page_directory(page_directory);
        memset(pcb, 0, sizeof(*pcb));
        if (process_manager_state.active_process_count > 0) {
            process_manager_state.active_process_count--;
        }
        return true;
    }
    return false;
}
