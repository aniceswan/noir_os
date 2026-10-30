#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

static uint8_t fb_row = 0;
static uint8_t fb_col = 0;

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t position = (uint16_t) r * FRAMEBUFFER_WIDTH + c;

    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) (position >> 8));
    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) (position & 0xFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint32_t index = ((uint32_t) row * FRAMEBUFFER_WIDTH + col) * 2;

    FRAMEBUFFER_MEMORY_OFFSET[index]     = c;
    FRAMEBUFFER_MEMORY_OFFSET[index + 1] = (bg << 4) | (fg & 0x0F);
}

void framebuffer_clear(void) {
    for (uint32_t i = 0; i < FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT; i++) {
        FRAMEBUFFER_MEMORY_OFFSET[i * 2]     = 0x00;
        FRAMEBUFFER_MEMORY_OFFSET[i * 2 + 1] = 0x07;
    }
    fb_row = 0;
    fb_col = 0;
    framebuffer_set_cursor(0, 0);
}

static void framebuffer_scroll(void) {
    uint32_t row_size = FRAMEBUFFER_WIDTH * 2;
    memmove(FRAMEBUFFER_MEMORY_OFFSET,
            FRAMEBUFFER_MEMORY_OFFSET + row_size,
            row_size * (FRAMEBUFFER_HEIGHT - 1));
    memset(FRAMEBUFFER_MEMORY_OFFSET + row_size * (FRAMEBUFFER_HEIGHT - 1),
           0, row_size);
}

void framebuffer_putchar(char c, uint8_t color) {
    uint8_t fg = color & 0x0F;
    uint8_t bg = (color >> 4) & 0x0F;

    if (c == '\n') {
        fb_col = 0;
        fb_row++;
    } else if (c == '\r') {
        fb_col = 0;
    } else if (c == '\b') {
        if (fb_col > 0) {
            fb_col--;
        } else if (fb_row > 0) {
            fb_row--;
            fb_col = FRAMEBUFFER_WIDTH - 1;
        }
        framebuffer_write(fb_row, fb_col, ' ', fg, bg);
    } else {
        framebuffer_write(fb_row, fb_col, c, fg, bg);
        fb_col++;
        if (fb_col >= FRAMEBUFFER_WIDTH) {
            fb_col = 0;
            fb_row++;
        }
    }

    if (fb_row >= FRAMEBUFFER_HEIGHT) {
        framebuffer_scroll();
        fb_row = FRAMEBUFFER_HEIGHT - 1;
    }

    framebuffer_set_cursor(fb_row, fb_col);
}

void framebuffer_puts(const char *buffer, uint32_t count, uint8_t color) {
    for (uint32_t i = 0; i < count; i++) {
        framebuffer_putchar(buffer[i], color);
    }
}

void framebuffer_move_cursor(uint8_t row, uint8_t col) {
    if (row >= FRAMEBUFFER_HEIGHT) row = FRAMEBUFFER_HEIGHT - 1;
    if (col >= FRAMEBUFFER_WIDTH) col = FRAMEBUFFER_WIDTH - 1;
    fb_row = row;
    fb_col = col;
    framebuffer_set_cursor(fb_row, fb_col);
}

void framebuffer_get_cursor(uint8_t *row, uint8_t *col) {
    if (row != NULL) *row = fb_row;
    if (col != NULL) *col = fb_col;
}
