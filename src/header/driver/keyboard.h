#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/cpu/interrupt.h"

#define EXT_SCANCODE_UP        0x48
#define EXT_SCANCODE_DOWN      0x50
#define EXT_SCANCODE_LEFT      0x4B
#define EXT_SCANCODE_RIGHT     0x4D

#define KEYBOARD_DATA_PORT     0x60
#define EXTENDED_SCANCODE_BYTE 0xE0

// Modifier keys, scancode set 1 make codes (break code = make code | 0x80)
#define SCANCODE_RELEASE_BIT   0x80
#define SCANCODE_CTRL          0x1D
#define SCANCODE_LEFT_SHIFT    0x2A
#define SCANCODE_RIGHT_SHIFT   0x36
#define SCANCODE_CAPS_LOCK     0x3A

/**
 * keyboard_scancode_1_to_ascii_map[256], Convert scancode values that correspond to ASCII printables
 * How to use this array: ascii_char = k[scancode]
 * 
 * By default, QEMU using scancode set 1 (from empirical testing)
 */
extern const char keyboard_scancode_1_to_ascii_map[256];

/**
 * KeyboardDriverState - Contain all driver states
 * 
 * @param read_extended_mode Optional, can be used for signaling next read is extended scancode (ex. arrow keys)
 * @param keyboard_input_on  Indicate whether keyboard ISR is activated or not
 * @param keyboard_buffer    Storing keyboard input values in ASCII
 * @param shift_pressed      Left or right shift is currently held
 * @param ctrl_pressed       Left or right ctrl is currently held
 * @param caps_lock_on       Caps lock toggle state
 * @param interrupt_requested Ctrl+C was pressed and is not handled yet
 */
struct KeyboardDriverState {
    bool read_extended_mode;
    bool keyboard_input_on;
    char keyboard_buffer;
    bool shift_pressed;
    bool ctrl_pressed;
    bool caps_lock_on;
    bool interrupt_requested;
} __attribute((packed));




/* -- Driver Interfaces -- */

// Activate keyboard ISR / start listen keyboard & save to buffer
void keyboard_state_activate(void);

// Deactivate keyboard ISR / stop listening keyboard interrupt
void keyboard_state_deactivate(void);

// Get keyboard buffer value and flush the buffer - @param buf Pointer to char buffer
void get_keyboard_buffer(char *buf);

// Check and clear the pending Ctrl+C request - @return true when Ctrl+C was pressed
bool keyboard_consume_interrupt_request(void);

/* -- Keyboard Interrupt Service Routine -- */

/**
 * Handling keyboard interrupt & process scancodes into ASCII character.
 * Will start listen and process keyboard scancode if keyboard_input_on.
 */
void keyboard_isr(void);

#endif
