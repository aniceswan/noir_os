#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode  = false,
    .keyboard_input_on   = false,
    .keyboard_buffer     = 0,
    .shift_pressed       = false,
    .ctrl_pressed        = false,
    .caps_lock_on        = false,
    .interrupt_requested = false,
};

/**
 * Character produced by a key while shift is held (US keyboard layout)
 */
static char keyboard_shift_char(char c) {
    if (c >= 'a' && c <= 'z') return (char) (c - 'a' + 'A');
    switch (c) {
        case '1':  return '!';
        case '2':  return '@';
        case '3':  return '#';
        case '4':  return '$';
        case '5':  return '%';
        case '6':  return '^';
        case '7':  return '&';
        case '8':  return '*';
        case '9':  return '(';
        case '0':  return ')';
        case '-':  return '_';
        case '=':  return '+';
        case '[':  return '{';
        case ']':  return '}';
        case ';':  return ':';
        case '\'': return '"';
        case '`':  return '~';
        case '\\': return '|';
        case ',':  return '<';
        case '.':  return '>';
        case '/':  return '?';
        default:   return c;
    }
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}

bool keyboard_consume_interrupt_request(void) {
    bool requested = keyboard_state.interrupt_requested;
    keyboard_state.interrupt_requested = false;
    return requested;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
    } else {
        bool is_extended = keyboard_state.read_extended_mode;
        keyboard_state.read_extended_mode = false;

        // Scancode set 1: bit 7 is set when the key is released (break code)
        bool    is_release = (scancode & SCANCODE_RELEASE_BIT) != 0;
        uint8_t key        = scancode & ~SCANCODE_RELEASE_BIT;

        if (key == SCANCODE_CTRL) {
            // Left ctrl, or right ctrl when prefixed with the extended byte
            keyboard_state.ctrl_pressed = !is_release;
        } else if (is_extended) {
            // Arrow keys & other extended keys (including fake shift) are ignored
        } else if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
            keyboard_state.shift_pressed = !is_release;
        } else if (key == SCANCODE_CAPS_LOCK) {
            if (!is_release) keyboard_state.caps_lock_on = !keyboard_state.caps_lock_on;
        } else if (!is_release && keyboard_state.keyboard_input_on) {
            char c = keyboard_scancode_1_to_ascii_map[key];
            if (keyboard_state.ctrl_pressed) {
                // Ctrl+C asks the kernel to stop the foreground process
                if (c == 'c') keyboard_state.interrupt_requested = true;
            } else if (c) {
                bool is_letter = (c >= 'a' && c <= 'z');
                bool use_shift = keyboard_state.shift_pressed;
                // Caps lock only affects letters and is cancelled by shift
                if (is_letter && keyboard_state.caps_lock_on) use_shift = !use_shift;
                keyboard_state.keyboard_buffer = use_shift ? keyboard_shift_char(c) : c;
            }
        }
    }

    // Semua IRQ1 wajib di-ACK, baik saat pembacaan on maupun off
    pic_ack(IRQ_KEYBOARD);
}
