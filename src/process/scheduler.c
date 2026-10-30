#include "header/process/scheduler.h"
#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

#define PIT_MAX_FREQUENCY   1193182
#define PIT_TIMER_FREQUENCY 1000
#define PIT_TIMER_COUNTER   (PIT_MAX_FREQUENCY / PIT_TIMER_FREQUENCY)

#define PIT_COMMAND_REGISTER_PIO          0x43
#define PIT_COMMAND_VALUE_BINARY_MODE     0b0
#define PIT_COMMAND_VALUE_OPR_SQUARE_WAVE (0b011 << 1)
#define PIT_COMMAND_VALUE_ACC_LOHIBYTE    (0b11  << 4)
#define PIT_COMMAND_VALUE_CHANNEL         (0b00  << 6)
#define PIT_COMMAND_VALUE (PIT_COMMAND_VALUE_BINARY_MODE | PIT_COMMAND_VALUE_OPR_SQUARE_WAVE | PIT_COMMAND_VALUE_ACC_LOHIBYTE | PIT_COMMAND_VALUE_CHANNEL)

#define PIT_CHANNEL_0_DATA_PIO 0x40

static void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    // Unmask IRQ0 (timer)
    out(PIC1_DATA, in(PIC1_DATA) & (uint8_t) ~(1 << IRQ_TIMER));
}

void scheduler_init(void) {
    /* Process slots are zero-initialized (PROCESS_STATE_UNUSED). Do NOT reset
     * them here: the init process is created before scheduler_init() runs. */
    process_manager_state.current_process_index = PROCESS_COUNT_MAX;
    activate_timer_interrupt();
}

bool scheduler_has_runnable_process(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_READY
            || _process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return true;
        }
    }
    return false;
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb == NULL) return;
    pcb->context = ctx;
    if (pcb->metadata.state == PROCESS_STATE_RUNNING) {
        pcb->metadata.state = PROCESS_STATE_READY;
    }
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    uint32_t start = 0;
    if (process_manager_state.current_process_index < PROCESS_COUNT_MAX) {
        start = (process_manager_state.current_process_index + 1) % PROCESS_COUNT_MAX;
    }

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        uint32_t index = (start + i) % PROCESS_COUNT_MAX;
        struct ProcessControlBlock *pcb = &_process_list[index];
        if (pcb->metadata.state != PROCESS_STATE_READY
            && pcb->metadata.state != PROCESS_STATE_RUNNING) {
            continue;
        }

        process_manager_state.current_process_index = index;
        pcb->metadata.state = PROCESS_STATE_RUNNING;
        paging_use_page_directory(pcb->context.page_directory_virtual_addr);
        process_context_switch(pcb->context);
    }

    /* No runnable process: idle until the next interrupt finds one. */
    process_manager_state.current_process_index = PROCESS_COUNT_MAX;
    __asm__ volatile("sti");
    while (true) {
        __asm__ volatile("hlt");
    }
    __builtin_unreachable();
}
