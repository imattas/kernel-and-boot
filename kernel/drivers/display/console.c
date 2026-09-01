#include "console.h"

#define CONSOLE_GLYPH_WIDTH  6U
#define CONSOLE_GLYPH_HEIGHT 8U

static framebuffer_t *console_framebuffer;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint8_t escape_state;
static uint32_t escape_parameter;

static void console_clear_pixels(void) {
    uint32_t total = console_framebuffer->height * console_framebuffer->pitch_pixels;
    for (uint32_t index = 0; index < total; ++index)
        console_framebuffer->pixels[index] = 0;
    cursor_x = 0;
    cursor_y = 0;
}

static int console_control(char value) {
    if (escape_state == 0) {
        if ((uint8_t)value == 0x1b) {
            escape_state = 1;
            return 1;
        }
        return 0;
    }
    if (escape_state == 1) {
        if (value == '[') {
            escape_state = 2;
            escape_parameter = 0;
            return 1;
        }
        escape_state = 0;
        return 1;
    }
    if (value >= '0' && value <= '9') {
        if (escape_parameter <= 100000U)
            escape_parameter = escape_parameter * 10U + (uint32_t)(value - '0');
        return 1;
    }
    if (value == ';') return 1;
    if (value == 'J') {
        if (escape_parameter == 0 || escape_parameter == 2)
            console_clear_pixels();
    } else if (value == 'H' || value == 'f') {
        cursor_x = 0;
        cursor_y = 0;
    } else if (value == 'K') {
        uint32_t width = console_framebuffer->width;
        uint32_t y = cursor_y * CONSOLE_GLYPH_HEIGHT;
        for (uint32_t row = 0; row < CONSOLE_GLYPH_HEIGHT && y + row <
             console_framebuffer->height; ++row)
            for (uint32_t x = cursor_x * CONSOLE_GLYPH_WIDTH; x < width; ++x)
                console_framebuffer->pixels[(uint64_t)(y + row) *
                    console_framebuffer->pitch_pixels + x] = 0;
    }
    escape_state = 0;
    escape_parameter = 0;
    return 1;
}

static uint8_t glyph_row(char value, uint32_t row) {
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    if (row >= 7) return 0;
    if (value >= '0' && value <= '9') return digits[(uint32_t)value - '0'][row];
    if (value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
    if (value >= 'A' && value <= 'Z') return letters[(uint32_t)value - 'A'][row];
    if (value == '-') return row == 3 ? 31 : 0;
    if (value == '_') return row == 6 ? 31 : 0;
    if (value == '.') return row == 6 ? 4 : 0;
    if (value == ':') return (row == 2 || row == 5) ? 4 : 0;
    if (value == '/') return (row == 0 || row == 6) ? 1 : (uint8_t)(1U << (6U - row));
    if (value == '>') return (row == 2 || row == 4) ? (uint8_t)(1U << (row == 2 ? 4 : 0)) :
                                      (row == 3 ? 8 : 0);
    if (value == '<') return (row == 2 || row == 4) ? (uint8_t)(1U << (row == 2 ? 0 : 4)) :
                                      (row == 3 ? 2 : 0);
    if (value == '!') return row == 6 ? 4 : 4;
    if (value == '?') return row == 0 ? 14 : (row == 1 ? 17 : (row == 2 ? 1 :
                                      (row == 3 ? 2 : (row == 6 ? 4 : 0))));
    return 0;
}

static void console_scroll(void) {
    uint32_t lines = console_framebuffer->height / CONSOLE_GLYPH_HEIGHT;
    uint32_t row_pixels = CONSOLE_GLYPH_HEIGHT * console_framebuffer->pitch_pixels;
    uint32_t total = console_framebuffer->height * console_framebuffer->pitch_pixels;
    for (uint32_t index = row_pixels; index < total; ++index)
        console_framebuffer->pixels[index - row_pixels] = console_framebuffer->pixels[index];
    for (uint32_t index = total - row_pixels; index < total; ++index)
        console_framebuffer->pixels[index] = 0;
    cursor_y = lines ? lines - 1U : 0;
}

static void console_character(char value) {
    if (console_control(value)) return;
    if (value == '\r') { cursor_x = 0; return; }
    if (value == '\n') { cursor_x = 0; ++cursor_y; }
    else if (value == '\b') { if (cursor_x) --cursor_x; }
    else {
        uint32_t x = cursor_x * CONSOLE_GLYPH_WIDTH;
        uint32_t y = cursor_y * CONSOLE_GLYPH_HEIGHT;
        for (uint32_t row = 0; row < 7; ++row)
            for (uint32_t column = 0; column < 5; ++column)
                if (glyph_row(value, row) & (1U << (4U - column)))
                    console_framebuffer->pixels[(uint64_t)(y + row) * console_framebuffer->pitch_pixels + x + column] = 0xffffffff;
        ++cursor_x;
    }
    if (cursor_x >= console_framebuffer->width / CONSOLE_GLYPH_WIDTH) {
        cursor_x = 0; ++cursor_y;
    }
    if (cursor_y >= console_framebuffer->height / CONSOLE_GLYPH_HEIGHT) console_scroll();
}

int console_initialize(framebuffer_t *framebuffer) {
    if (!framebuffer || !framebuffer->pixels || framebuffer->width < CONSOLE_GLYPH_WIDTH ||
        framebuffer->height < CONSOLE_GLYPH_HEIGHT) return 0;
    console_framebuffer = framebuffer;
    cursor_x = cursor_y = 0;
    escape_state = 0;
    escape_parameter = 0;
    framebuffer_clear(framebuffer, 0);
    return 1;
}

uint32_t console_write(const void *buffer, uint32_t length) {
    if (!console_framebuffer || !buffer) return 0;
    uint64_t flags = spinlock_lock_irqsave(&console_framebuffer->lock);
    const char *text = (const char *)buffer;
    for (uint32_t index = 0; index < length; ++index) console_character(text[index]);
    spinlock_unlock_irqrestore(&console_framebuffer->lock, flags);
    return length;
}
