#include "header/process/scheduler.h"
#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/cpu/interrupt.h"
#include "header/text/framebuffer.h"

// PCB index of the process that got the last time slice, -1 before the first switch
static int32_t scheduler_last_index = -1;

void scheduler_init(void) {
    scheduler_last_index = -1;
    // From now on IRQ0 fires periodically and preempts the running process
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    if (current_pcb == NULL) return;
    current_pcb->context = ctx;
}

/**
 * Round Robin: walk the process list circularly, starting right after the
 * process that ran last, and take the first READY process. The search ends
 * on the last process itself, so a lone process simply keeps running.
 *
 * @return PCB index of the next process, -1 when nothing is ready
 */
static int32_t scheduler_get_next_ready_index(void) {
    for (int32_t i = 1; i <= PROCESS_COUNT_MAX; i++) {
        int32_t index = (scheduler_last_index + i) % PROCESS_COUNT_MAX;
        if (_process_list[index].metadata.active &&
            _process_list[index].metadata.state == PROCESS_STATE_READY) {
            return index;
        }
    }
    return -1;
}

__attribute__((noreturn)) static void scheduler_halt(void) {
    static const char message[] = "\nscheduler: no runnable process, system halted\n";
    framebuffer_puts(message, sizeof(message) - 1, 0x0C);
    while (true) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    // The running process goes back to the queue
    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    if (current_pcb != NULL) current_pcb->metadata.state = PROCESS_STATE_READY;

    // The system always needs its init process (the shell). Start it again
    // when it was killed or crashed, otherwise nobody could type a command.
    if (process_get_pcb_by_pid(process_manager_state.init_pid) == NULL) {
        process_create_init_process();
    }

    int32_t next_index = scheduler_get_next_ready_index();
    if (next_index < 0) scheduler_halt();

    struct ProcessControlBlock *next_pcb = &_process_list[next_index];
    next_pcb->metadata.state = PROCESS_STATE_RUNNING;
    scheduler_last_index     = next_index;

    // Switch virtual address space first, then CPU register with iret
    paging_use_page_directory(next_pcb->context.page_directory_virtual_addr);
    process_context_switch(next_pcb->context);
}
