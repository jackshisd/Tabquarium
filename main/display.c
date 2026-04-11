#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fish_catalog.h"
#include "fish_names.h"
#include "aquarium.h"

static const char *TAG = "DISPLAY";

#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_I2C_PORT I2C_NUM_0

#if CONFIG_IDF_TARGET_ESP32C3
#define SSD1306_SDA_GPIO GPIO_NUM_5
#define SSD1306_SCL_GPIO GPIO_NUM_6
#else
#define SSD1306_SDA_GPIO GPIO_NUM_22
#define SSD1306_SCL_GPIO GPIO_NUM_23
#endif

static uint8_t display_buffer[1024];
static uint8_t decor_base_buffer[1024];
static uint8_t decor_mask_buffer[1024];

typedef struct {
    int x;
    int y;
    int state;
    int age;
    int speed;
    bool active;
} rain_drop_t;

static rain_drop_t rain_drops[12];
static int last_rain_frame = -1;
static int sun_ray_origin_x = -1;
static int sun_ray_beam_count = 0;

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5f,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7f,0x14,0x7f,0x14}, /* # */
    {0x24,0x2a,0x7f,0x2a,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1c,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1c,0x00}, /* ) */
    {0x14,0x08,0x3e,0x08,0x14}, /* * */
    {0x08,0x08,0x3e,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3e,0x51,0x49,0x45,0x3e}, /* 0 */
    {0x00,0x42,0x7f,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4b,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7f,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3c,0x4a,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1e}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3e}, /* @ */
    {0x7e,0x11,0x11,0x11,0x7e}, /* A */
    {0x7f,0x49,0x49,0x49,0x36}, /* B */
    {0x3e,0x41,0x41,0x41,0x22}, /* C */
    {0x7f,0x41,0x41,0x22,0x1c}, /* D */
    {0x7f,0x49,0x49,0x49,0x41}, /* E */
    {0x7f,0x09,0x09,0x09,0x01}, /* F */
    {0x3e,0x41,0x49,0x49,0x7a}, /* G */
    {0x7f,0x08,0x08,0x08,0x7f}, /* H */
    {0x00,0x41,0x7f,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3f,0x01}, /* J */
    {0x7f,0x08,0x14,0x22,0x41}, /* K */
    {0x7f,0x40,0x40,0x40,0x40}, /* L */
    {0x7f,0x02,0x0c,0x02,0x7f}, /* M */
    {0x7f,0x04,0x08,0x10,0x7f}, /* N */
    {0x3e,0x41,0x41,0x41,0x3e}, /* O */
    {0x7f,0x09,0x09,0x09,0x06}, /* P */
    {0x3e,0x41,0x51,0x21,0x5e}, /* Q */
    {0x7f,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7f,0x01,0x01}, /* T */
    {0x3f,0x40,0x40,0x40,0x3f}, /* U */
    {0x1f,0x20,0x40,0x20,0x1f}, /* V */
    {0x7f,0x20,0x18,0x20,0x7f}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};

static int font_index(char c)
{
    if (c == ' ') {
        return 0;
    }
    if (c == '!') {
        return 1;
    }
    if (c == '"') {
        return 2;
    }
    if (c == '#') {
        return 3;
    }
    if (c == '$') {
        return 4;
    }
    if (c == '%') {
        return 5;
    }
    if (c == '&') {
        return 6;
    }
    if (c == '\'') {
        return 7;
    }
    if (c == '(') {
        return 8;
    }
    if (c == ')') {
        return 9;
    }
    if (c == '*') {
        return 10;
    }
    if (c == '+') {
        return 11;
    }
    if (c == ',') {
        return 12;
    }
    if (c == '-') {
        return 13;
    }
    if (c == '.') {
        return 14;
    }
    if (c == '/') {
        return 15;
    }
    if (c >= '0' && c <= '9') {
        return 16 + (c - '0');
    }
    if (c == ':') {
        return 26;
    }
    if (c == ';') {
        return 27;
    }
    if (c == '<') {
        return 28;
    }
    if (c == '=') {
        return 29;
    }
    if (c == '>') {
        return 30;
    }
    if (c == '?') {
        return 31;
    }
    if (c == '@') {
        return 32;
    }

    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') {
        return 33 + (c - 'A');
    }

    return 0;
}

static void display_set_pixel(int x, int y, bool on);
static void display_draw_decoration(decor_kind_t kind, int x, int y);
static void display_draw_dotted_line(int x1, int y1, int x2, int y2);

static void display_draw_text(int x, int y, const char *text)
{
    while (*text) {
        int index = font_index(*text++);
        for (int column = 0; column < 5; column++) {
            uint8_t bits = font5x7[index][column];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    int draw_x = x + column;
                    int draw_y = y + row;
                    if (draw_x >= 0 && draw_x < SCREEN_WIDTH && draw_y >= 0 && draw_y < SCREEN_HEIGHT) {
                        int byte_index = (draw_y / 8) * SCREEN_WIDTH + draw_x;
                        display_buffer[byte_index] |= (1 << (draw_y % 8));
                    }
                }
            }
        }
        x += 6;
    }
}

static void display_draw_scaled_text(int x, int y, const char *text, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    // Keep glyphs large but reduce stroke thickness so digits look less chunky.
    int stroke = (scale >= 4) ? 2 : 1;

    while (*text) {
        int index = font_index(*text++);
        for (int column = 0; column < 5; column++) {
            uint8_t bits = font5x7[index][column];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1 << row))) {
                    continue;
                }

                int px = x + (column * scale);
                int py = y + (row * scale);
                for (int sy = 0; sy < stroke; sy++) {
                    for (int sx = 0; sx < stroke; sx++) {
                        display_set_pixel(px + sx, py + sy, true);
                    }
                }
            }
        }

        x += 6 * scale;
    }
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void display_draw_tiny_sleep_z(int x, int y)
{
    // Compact 3x4 "Z" for sleep particles so it reads as a hint, not UI text.
    display_set_pixel(x, y, true);
    display_set_pixel(x + 1, y, true);
    display_set_pixel(x + 2, y, true);
    display_set_pixel(x + 2, y + 1, true);
    display_set_pixel(x + 1, y + 2, true);
    display_set_pixel(x, y + 3, true);
    display_set_pixel(x + 1, y + 3, true);
    display_set_pixel(x + 2, y + 3, true);
}

static void display_draw_number(int x, int y, int value)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    display_draw_text(x, y, buffer);
}

static void display_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    int byte_index = (y / 8) * SCREEN_WIDTH + x;
    int bit = y % 8;

    if (on) {
        display_buffer[byte_index] |= (1 << bit);
    } else {
        display_buffer[byte_index] &= ~(1 << bit);
    }
}

static bool display_get_pixel_from_buffer(const uint8_t *buffer, int x, int y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return false;
    }

    int byte_index = (y / 8) * SCREEN_WIDTH + x;
    int bit = y % 8;
    return (buffer[byte_index] & (1 << bit)) != 0;
}

static void display_set_pixel_in_buffer(uint8_t *buffer, int x, int y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    int byte_index = (y / 8) * SCREEN_WIDTH + x;
    int bit = y % 8;
    buffer[byte_index] |= (1 << bit);
}

static void display_draw_decoration_bottom_aligned(decor_kind_t kind, int x, int y)
{
    memcpy(decor_base_buffer, display_buffer, sizeof(display_buffer));
    memset(display_buffer, 0, sizeof(display_buffer));

    display_draw_decoration(kind, x, y);

    int bottom_y = -1;
    int fallback_bottom_y = -1;
    for (int scan_y = SCREEN_HEIGHT - 1; scan_y >= 0; scan_y--) {
        int lit_pixels = 0;
        for (int scan_x = 0; scan_x < SCREEN_WIDTH; scan_x++) {
            if (display_get_pixel_from_buffer(display_buffer, scan_x, scan_y)) {
                lit_pixels++;
            }
        }

        if (lit_pixels > 0 && fallback_bottom_y < 0) {
            fallback_bottom_y = scan_y;
        }

        // Ignore single-pixel outliers when choosing the decor baseline.
        if (lit_pixels >= 2) {
            bottom_y = scan_y;
            break;
        }
    }

    if (bottom_y < 0) {
        bottom_y = fallback_bottom_y;
    }

    memset(decor_mask_buffer, 0, sizeof(decor_mask_buffer));
    if (bottom_y >= 0) {
        int shift_y = (SCREEN_HEIGHT - 1) - bottom_y;
        for (int scan_y = 0; scan_y < SCREEN_HEIGHT; scan_y++) {
            for (int scan_x = 0; scan_x < SCREEN_WIDTH; scan_x++) {
                if (!display_get_pixel_from_buffer(display_buffer, scan_x, scan_y)) {
                    continue;
                }

                display_set_pixel_in_buffer(decor_mask_buffer, scan_x, scan_y + shift_y);
            }
        }
    }

    memcpy(display_buffer, decor_base_buffer, sizeof(display_buffer));
    for (int scan_y = 0; scan_y < SCREEN_HEIGHT; scan_y++) {
        for (int scan_x = 0; scan_x < SCREEN_WIDTH; scan_x++) {
            if (display_get_pixel_from_buffer(decor_mask_buffer, scan_x, scan_y)) {
                display_set_pixel(scan_x, scan_y, true);
            }
        }
    }
}

static void display_clear(void)
{
    memset(display_buffer, 0, sizeof(display_buffer));
}

static void display_send_command(uint8_t cmd)
{
    uint8_t buffer[2] = {0x00, cmd};
    i2c_master_write_to_device(SSD1306_I2C_PORT, SSD1306_I2C_ADDR, buffer, sizeof(buffer), pdMS_TO_TICKS(1000));
}

static void display_send_data(uint8_t *data, uint16_t len)
{
    uint8_t *temp = malloc(len + 1);
    if (temp == NULL) {
        return;
    }

    temp[0] = 0x40;
    memcpy(temp + 1, data, len);
    i2c_master_write_to_device(SSD1306_I2C_PORT, SSD1306_I2C_ADDR, temp, len + 1, pdMS_TO_TICKS(1000));
    free(temp);
}

static void display_draw_line(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int error = dx - dy;

    while (true) {
        display_set_pixel(x1, y1, true);
        if (x1 == x2 && y1 == y2) {
            break;
        }

        int error2 = 2 * error;
        if (error2 > -dy) {
            error -= dy;
            x1 += sx;
        }
        if (error2 < dx) {
            error += dx;
            y1 += sy;
        }
    }
}

static void display_draw_rect(int x, int y, int w, int h)
{
    display_draw_line(x, y, x + w, y);
    display_draw_line(x + w, y, x + w, y + h);
    display_draw_line(x + w, y + h, x, y + h);
    display_draw_line(x, y + h, x, y);
}

static void display_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int yy = y; yy <= (y + h); yy++) {
        for (int xx = x; xx <= (x + w); xx++) {
            display_set_pixel(xx, yy, on);
        }
    }
}

static void display_invert_rect(int x, int y, int w, int h)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= SCREEN_WIDTH) x1 = SCREEN_WIDTH - 1;
    if (y1 >= SCREEN_HEIGHT) y1 = SCREEN_HEIGHT - 1;

    for (int yy = y0; yy <= y1; yy++) {
        for (int xx = x0; xx <= x1; xx++) {
            int byte_index = (yy / 8) * SCREEN_WIDTH + xx;
            display_buffer[byte_index] ^= (1 << (yy % 8));
        }
    }
}

static void display_highlight_row(int row_y)
{
    display_invert_rect(0, row_y, SCREEN_WIDTH - 1, 9);
}

static int collect_owned_decor_kinds(decor_kind_t *out, int max_count)
{
#if DECOR_PREVIEW_TEST_BUILD
    int count = 0;
    for (int kind = 0; kind < DECOR_KIND_COUNT && count < max_count; kind++) {
        out[count++] = (decor_kind_t)kind;
    }
    return count;
#else
    int count = 0;
    for (int kind = 0; kind < DECOR_KIND_COUNT && count < max_count; kind++) {
        if (aquarium_is_decor_owned((decor_kind_t)kind)) {
            out[count++] = (decor_kind_t)kind;
        }
    }

    return count;
#endif
}

static void display_draw_circle(int x, int y, int radius, bool fill)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int distance_sq = dx * dx + dy * dy;
            if ((fill && distance_sq <= radius * radius) ||
                (!fill && distance_sq <= radius * radius && distance_sq >= (radius - 1) * (radius - 1))) {
                display_set_pixel(x + dx, y + dy, true);
            }
        }
    }
}

static void display_draw_fish(int x, int y, int size, bool facing_right)
{
    int body_half = size / 2;
    display_draw_circle(x, y, size / 2, true);
    display_draw_circle(x + (facing_right ? size / 4 : -size / 4), y, size / 3, true);

    if (facing_right) {
        int tail_root_x = x - body_half - 1;
        int tail_tip_x = tail_root_x - (size / 3);
        display_draw_line(tail_root_x, y, tail_tip_x, y - size / 2);
        display_draw_line(tail_root_x, y, tail_tip_x, y + size / 2);
        display_draw_line(tail_tip_x, y - size / 2, tail_tip_x, y + size / 2);
    } else {
        int tail_root_x = x + body_half + 1;
        int tail_tip_x = tail_root_x + (size / 3);
        display_draw_line(tail_root_x, y, tail_tip_x, y - size / 2);
        display_draw_line(tail_root_x, y, tail_tip_x, y + size / 2);
        display_draw_line(tail_tip_x, y - size / 2, tail_tip_x, y + size / 2);
    }
}

static void display_draw_pufferfish(int x, int y, int size, bool facing_right)
{
    int body_rx = (size / 2);
    int body_ry = (size / 3);
    if (body_ry < 1) {
        body_ry = 1;
    }
    int head_offset = facing_right ? (body_rx / 2) : -(body_rx / 2);

    // Body and head
    display_draw_circle(x, y, body_ry + 1, true);
    display_fill_rect(x - body_rx, y - body_ry, body_rx * 2, body_ry * 2, true);
    display_draw_circle(x + head_offset, y, body_ry, true);

    // Fan-like dorsal/ventral spines
    display_draw_line(x - 1, y - body_ry - 1, x - 3, y - body_ry - 2);
    display_draw_line(x + 1, y - body_ry - 1, x + 1, y - body_ry - 2);
    display_draw_line(x + 2, y - body_ry - 1, x + 4, y - body_ry - 2);
    display_draw_line(x - 1, y + body_ry + 1, x - 3, y + body_ry + 2);
    display_draw_line(x + 1, y + body_ry + 1, x + 1, y + body_ry + 2);
    display_draw_line(x + 3, y + body_ry + 1, x + 5, y + body_ry + 2);

    // Tail
    if (facing_right) {
        display_draw_line(x - body_rx - 1, y, x - body_rx - 3, y - 1);
        display_draw_line(x - body_rx - 1, y, x - body_rx - 3, y + 1);
        display_draw_line(x - body_rx - 3, y - 1, x - body_rx - 3, y + 1);
    } else {
        display_draw_line(x + body_rx + 1, y, x + body_rx + 3, y - 1);
        display_draw_line(x + body_rx + 1, y, x + body_rx + 3, y + 1);
        display_draw_line(x + body_rx + 3, y - 1, x + body_rx + 3, y + 1);
    }

    // Stripe hints
    display_draw_line(x - 3, y - body_ry, x - 3, y + body_ry);
    display_draw_line(x, y - body_ry, x, y + body_ry);
    display_draw_line(x + 3, y - body_ry, x + 3, y + body_ry);
}

static void display_draw_lionfish(int x, int y, int size, bool facing_right)
{
    int dir = facing_right ? 1 : -1;
    int body_len = (size / 2) + 1;
    int body_h = (size / 4) + 1;
    if (body_len < 2) {
        body_len = 2;
    }
    if (body_h < 1) {
        body_h = 1;
    }

    int nose_x = x + (dir * body_len);
    int tail_x = x - (dir * body_len);

    // Lean, angular lionfish body.
    display_draw_line(tail_x, y, x - dir, y - body_h);
    display_draw_line(x - dir, y - body_h, nose_x, y);
    display_draw_line(nose_x, y, x + dir, y + body_h);
    display_draw_line(x + dir, y + body_h, tail_x, y);

    // Narrow head and eye.
    display_set_pixel(nose_x - (dir * 1), y - 1, true);
    display_draw_line(nose_x - (dir * 2), y - 1, nose_x - (dir * 2), y + 1);

    // Spiky dorsal fins.
    display_draw_line(x - 2, y - body_h, x - 3, y - body_h - 3);
    display_draw_line(x, y - body_h - 1, x, y - body_h - 4);
    display_draw_line(x + 2, y - body_h, x + 3, y - body_h - 3);
    display_draw_line(x + 4, y - body_h, x + 5, y - body_h - 2);

    // Sharp ventral fins.
    display_draw_line(x - 1, y + body_h, x - 2, y + body_h + 2);
    display_draw_line(x + 1, y + body_h + 1, x + 1, y + body_h + 3);
    display_draw_line(x + 3, y + body_h, x + 4, y + body_h + 2);

    // Long pectoral fins for the lionfish silhouette.
    display_draw_line(x - (dir * 1), y - 1, x - (dir * 4), y - 3);
    display_draw_line(x - (dir * 1), y + 1, x - (dir * 4), y + 3);

    // Forked tail.
    display_draw_line(tail_x, y, tail_x - (dir * 2), y - 2);
    display_draw_line(tail_x, y, tail_x - (dir * 2), y + 2);

    // Subtle stripes.
    display_draw_line(x - 1, y - body_h, x - 1, y + body_h);
    display_draw_line(x + 2, y - body_h, x + 2, y + body_h);
}

static void display_draw_clownfish(int x, int y, int size, bool facing_right)
{
    int body_r = (size / 2) + 1;
    int head_dx = facing_right ? (body_r / 2) : -(body_r / 2);

    // Rounded clownfish body
    display_draw_circle(x, y, body_r, true);
    display_draw_circle(x + head_dx, y, body_r - 1, true);

    // Tail
    if (facing_right) {
        display_draw_line(x - body_r, y, x - body_r - 4, y - 3);
        display_draw_line(x - body_r, y, x - body_r - 4, y + 3);
        display_draw_line(x - body_r - 4, y - 3, x - body_r - 4, y + 3);
    } else {
        display_draw_line(x + body_r, y, x + body_r + 4, y - 3);
        display_draw_line(x + body_r, y, x + body_r + 4, y + 3);
        display_draw_line(x + body_r + 4, y - 3, x + body_r + 4, y + 3);
    }

    // Three white bands (carved out for monochrome readability)
    display_fill_rect(x - 3, y - body_r, 1, body_r * 2, false);
    display_fill_rect(x, y - body_r, 1, body_r * 2, false);
    display_fill_rect(x + 3, y - body_r, 1, body_r * 2, false);

    // Band outlines for clarity
    display_draw_line(x - 4, y - body_r, x - 4, y + body_r);
    display_draw_line(x + 1, y - body_r, x + 1, y + body_r);
    display_draw_line(x + 4, y - body_r, x + 4, y + body_r);
}

static void display_draw_shark(int x, int y, int size, bool facing_right)
{
    int dir = facing_right ? 1 : -1;
    int body_len = size + 3;
    int body_h = (size / 3) + 1;
    if (body_h < 1) {
        body_h = 1;
    }
    int nose_x = x + (dir * body_len);
    int tail_base_x = x - (dir * body_len);

    // Main body: pointed nose, thicker belly, tapered tail base.
    display_draw_line(tail_base_x, y - 1, x - dir, y - body_h);
    display_draw_line(x - dir, y - body_h, nose_x, y);
    display_draw_line(nose_x, y, x + dir, y + body_h + 1);
    display_draw_line(x + dir, y + body_h + 1, tail_base_x, y + 1);
    display_draw_line(tail_base_x, y + 1, tail_base_x, y - 1);

    // Tall triangular dorsal fin (Jaws-like silhouette).
    int dorsal_base_front_x = x - (dir * 1);
    int dorsal_base_rear_x = x - (dir * 4);
    int dorsal_tip_x = x - (dir * 2);
    display_draw_line(dorsal_base_front_x, y - body_h, dorsal_tip_x, y - body_h - 4);
    display_draw_line(dorsal_tip_x, y - body_h - 4, dorsal_base_rear_x, y - body_h);
    display_draw_line(dorsal_base_rear_x, y - body_h, dorsal_base_front_x, y - body_h);

    // Pectoral fin and small rear fin.
    display_draw_line(x + (dir * 2), y + 1, x + (dir * 5), y + body_h + 2);
    display_draw_line(x - (dir * 4), y - body_h + 1, x - (dir * 6), y - body_h - 1);

    // Shark tail with asymmetry (upper lobe slightly longer).
    display_draw_line(tail_base_x, y, tail_base_x - (dir * 3), y - body_h - 1);
    display_draw_line(tail_base_x, y, tail_base_x - (dir * 2), y + body_h);

    // Eye and gill slit.
    display_set_pixel(nose_x - (dir * 2), y - 1, true);
    display_draw_line(nose_x - (dir * 4), y - 1, nose_x - (dir * 4), y + 1);
}

static void display_draw_angelfish(int x, int y, int size, bool facing_right)
{
    int body_h = (size / 2) + 1;
    int body_w = (size / 4);
    if (body_h < 2) {
        body_h = 2;
    }
    if (body_w < 1) {
        body_w = 1;
    }
    int mouth_x = facing_right ? (x + body_w + 1) : (x - body_w - 1);

    // Narrow, tall body (shorter horizontally)
    display_draw_line(x, y - body_h, x + body_w, y);
    display_draw_line(x + body_w, y, x, y + body_h);
    display_draw_line(x, y + body_h, x - body_w, y);
    display_draw_line(x - body_w, y, x, y - body_h);
    display_draw_line(x, y - body_h + 1, x, y + body_h - 1);

    // Dorsal and anal fins
    display_draw_line(x, y - body_h, x, y - body_h - 2);
    display_draw_line(x, y + body_h, x, y + body_h + 2);

    // Tail fin (compact)
    if (facing_right) {
        display_draw_line(x - body_w, y, x - body_w - 2, y - 1);
        display_draw_line(x - body_w, y, x - body_w - 2, y + 1);
    } else {
        display_draw_line(x + body_w, y, x + body_w + 2, y - 1);
        display_draw_line(x + body_w, y, x + body_w + 2, y + 1);
    }

    // Mouth and center stripe
    display_set_pixel(mouth_x, y, true);
    display_draw_line(x - 1, y - 1, x - 1, y + 1);
}

static void display_draw_catfish(int x, int y, int size, bool facing_right)
{
    int body_len = size + 1;
    int body_h = (size / 2);
    int head_x = facing_right ? (x + body_len / 2) : (x - body_len / 2);
    int tail_root_x = facing_right ? (x - body_len / 2 - 1) : (x + body_len / 2 + 1);
    int tail_tip_x = tail_root_x + (facing_right ? -2 : 2);

    // Bottom-dweller body
    display_draw_line(x - body_len / 2, y - body_h, x + body_len / 2, y - body_h);
    display_draw_line(x - body_len / 2, y + body_h, x + body_len / 2, y + body_h);
    display_draw_line(x - body_len / 2, y - body_h, x - body_len / 2, y + body_h);
    display_draw_line(x + body_len / 2, y - body_h, x + body_len / 2, y + body_h);

    // Tail
    display_draw_line(tail_root_x, y, tail_tip_x, y - 2);
    display_draw_line(tail_root_x, y, tail_tip_x, y + 2);

    // Barbels (whiskers)
    display_draw_line(head_x, y, head_x + (facing_right ? 3 : -3), y - 1);
    display_draw_line(head_x, y, head_x + (facing_right ? 3 : -3), y + 1);
    display_draw_line(head_x - (facing_right ? 0 : 0), y + 1, head_x + (facing_right ? 2 : -2), y + 3);
}

static void display_draw_arowana(int x, int y, int size, bool facing_right)
{
    int body_len = ((size + 5) * 6) / 10;
    int body_h = ((((size / 3) + 1) * 6) / 10);
    if (body_len < 2) {
        body_len = 2;
    }
    if (body_h < 1) {
        body_h = 1;
    }
    int nose_x = facing_right ? (x + body_len) : (x - body_len);
    int tail_x = facing_right ? (x - body_len) : (x + body_len);

    // Long surface fish profile
    display_draw_line(tail_x, y - body_h, nose_x, y - 1);
    display_draw_line(tail_x, y + body_h, nose_x, y + 1);
    display_draw_line(nose_x, y - 1, nose_x, y + 1);

    // Upturned mouth and barbels
    display_set_pixel(nose_x + (facing_right ? 1 : -1), y - 1, true);
    display_set_pixel(nose_x + (facing_right ? 1 : -1), y - 1, true);

    // Tail and lateral scale line
    display_draw_line(tail_x, y, tail_x - (facing_right ? 3 : -3), y - 2);
    display_draw_line(tail_x, y, tail_x - (facing_right ? 3 : -3), y + 2);
    display_draw_line(x - 2, y, x + 2, y);
}

static void display_draw_piranha(int x, int y, int size, bool facing_right)
{
    int body_r = (size / 2) + 1;
    int mouth_x = facing_right ? (x + body_r + 1) : (x - body_r - 1);

    // Deep body
    display_draw_circle(x, y, body_r, false);
    display_draw_line(x - body_r + 1, y, x + body_r - 1, y);

    // Tail
    if (facing_right) {
        display_draw_line(x - body_r, y, x - body_r - 3, y - 2);
        display_draw_line(x - body_r, y, x - body_r - 3, y + 2);
    } else {
        display_draw_line(x + body_r, y, x + body_r + 3, y - 2);
        display_draw_line(x + body_r, y, x + body_r + 3, y + 2);
    }

    // Teeth suggestion
    display_set_pixel(mouth_x, y, true);
    display_set_pixel(mouth_x - (facing_right ? 1 : -1), y + 1, true);
    display_set_pixel(mouth_x - (facing_right ? 1 : -1), y - 1, true);
}

static void display_draw_hatchetfish(int x, int y, int size, bool facing_right)
{
    int body_w = (size / 2) + 2;
    int body_h = (size / 3) + 1;
    int front_x = facing_right ? (x + body_w) : (x - body_w);
    int rear_x = facing_right ? (x - body_w) : (x + body_w);

    // Hatchet profile: flatter top, deep chest
    display_draw_line(rear_x, y - body_h, front_x, y - body_h + 1);
    display_draw_line(rear_x, y - body_h, x, y + body_h + 2);
    display_draw_line(front_x, y - body_h + 1, x, y + body_h + 2);
    display_draw_line(rear_x + (facing_right ? 1 : -1), y, front_x - (facing_right ? 1 : -1), y);

    // Small tail
    display_draw_line(rear_x, y, rear_x - (facing_right ? 2 : -2), y - 1);
    display_draw_line(rear_x, y, rear_x - (facing_right ? 2 : -2), y + 1);
}

static void display_draw_snail(int x, int y, int size, int vx, int vy)
{
    int shell_r = (size - 2) / 4;
    if (shell_r < 1) {
        shell_r = 1;
    }

    int head_dx = 0;
    int head_dy = 1;
    if (abs(vx) >= abs(vy) && vx != 0) {
        head_dx = (vx > 0) ? 1 : -1;
        head_dy = 0;
    } else if (vy != 0) {
        head_dx = 0;
        head_dy = (vy > 0) ? 1 : -1;
    }

    // Top-down shell and spiral
    display_draw_circle(x, y, shell_r + 1, false);
    display_draw_circle(x, y, shell_r - 1, false);
    display_set_pixel(x, y, true);
    display_set_pixel(x + (head_dx == 0 ? 1 : 0), y + (head_dy == 0 ? 1 : 0), true);

    // Head and antennae in travel direction
    int head_x = x + head_dx * (shell_r + 2);
    int head_y = y + head_dy * (shell_r + 2);
    display_draw_circle(head_x, head_y, 1, true);

    int perp_x = -head_dy;
    int perp_y = head_dx;

    // Simple two-ball eyes without stalks.
    int eye_front = 1;
    int left_eye_x = head_x + perp_x + (head_dx * eye_front);
    int left_eye_y = head_y + perp_y + (head_dy * eye_front);
    int right_eye_x = head_x - perp_x + (head_dx * eye_front);
    int right_eye_y = head_y - perp_y + (head_dy * eye_front);

    display_set_pixel(left_eye_x, left_eye_y, true);
    display_set_pixel(right_eye_x, right_eye_y, true);
}

static void display_draw_crab(int x, int y, int size, bool facing_right)
{
    int body_r = (size - 2) / 6;
    if (body_r < 1) {
        body_r = 1;
    }

    int shell_w = body_r + 2;
    int leg_lift = ((x / 2) & 1);

    // Rounded shell
    display_draw_circle(x, y, body_r + 1, true);
    display_draw_line(x - shell_w, y, x + shell_w, y);
    display_draw_line(x - shell_w + 1, y - 1, x + shell_w - 1, y - 1);
    display_draw_line(x - shell_w + 1, y + 1, x + shell_w - 1, y + 1);

    // Eye stalks
    display_draw_line(x - 2, y - body_r - 1, x - 2, y - body_r + 1);
    display_draw_line(x + 2, y - body_r - 1, x + 2, y - body_r + 1);
    display_set_pixel(x - 2, y - body_r - 2, true);
    display_set_pixel(x + 2, y - body_r - 2, true);

    // Side legs with slight alternating lift while scuttling
    display_draw_line(x - shell_w + 1, y + 1, x - shell_w - 2, y + 3 - leg_lift);
    display_draw_line(x - shell_w + 2, y + 2, x - shell_w - 1, y + 4 - leg_lift);
    display_draw_line(x - shell_w + 3, y + 2, x - shell_w + 1, y + 4 - leg_lift);
    display_draw_line(x + shell_w - 1, y + 1, x + shell_w + 2, y + 3 - (1 - leg_lift));
    display_draw_line(x + shell_w - 2, y + 2, x + shell_w + 1, y + 4 - (1 - leg_lift));
    display_draw_line(x + shell_w - 3, y + 2, x + shell_w - 1, y + 4 - (1 - leg_lift));

    // Claws
    if (facing_right) {
        display_draw_line(x + shell_w - 1, y - 1, x + shell_w + 2, y - 3);
        display_draw_line(x + shell_w + 2, y - 3, x + shell_w + 4, y - 1);
        display_draw_line(x + shell_w + 2, y - 3, x + shell_w + 3, y - 4);
        display_draw_line(x - shell_w + 1, y - 1, x - shell_w - 1, y - 3);
    } else {
        display_draw_line(x - shell_w + 1, y - 1, x - shell_w - 2, y - 3);
        display_draw_line(x - shell_w - 2, y - 3, x - shell_w - 4, y - 1);
        display_draw_line(x - shell_w - 2, y - 3, x - shell_w - 3, y - 4);
        display_draw_line(x + shell_w - 1, y - 1, x + shell_w + 1, y - 3);
    }
}

static void display_draw_seaweed(int x, int y)
{
    display_draw_line(x, y + 8, x, y - 2);
    display_draw_line(x + 2, y + 8, x + 3, y + 1);
    display_draw_line(x + 4, y + 8, x + 6, y - 1);
    display_draw_line(x + 6, y + 8, x + 4, y + 2);
    display_draw_line(x + 8, y + 8, x + 8, y);
}

static void display_draw_castle(int x, int y)
{
    display_draw_line(x, y + 10, x, y);
    display_draw_line(x, y, x + 6, y);
    display_draw_line(x + 6, y, x + 6, y + 3);
    display_draw_line(x + 6, y + 3, x + 10, y + 3);
    display_draw_line(x + 10, y + 3, x + 10, y);
    display_draw_line(x + 10, y, x + 16, y);
    display_draw_line(x + 16, y, x + 16, y + 10);
    display_draw_line(x, y + 10, x + 16, y + 10);
    display_set_pixel(x + 4, y + 6, true);
    display_set_pixel(x + 12, y + 6, true);
}

static void display_draw_shipwreck(int x, int y)
{
    display_draw_line(x - 8, y + 8, x + 10, y + 4);
    display_draw_line(x - 8, y + 8, x - 4, y + 10);
    display_draw_line(x - 4, y + 10, x + 10, y + 6);
    display_draw_line(x + 1, y + 4, x + 6, y - 4);
    display_draw_line(x + 6, y - 4, x + 4, y - 8);
    display_draw_line(x + 6, y - 4, x + 9, y - 6);
}

static void display_draw_treasure_chest(int x, int y)
{
    display_draw_line(x - 8, y + 10, x + 8, y + 10);
    display_draw_line(x - 8, y + 10, x - 8, y + 4);
    display_draw_line(x + 8, y + 10, x + 8, y + 4);
    display_draw_line(x - 8, y + 4, x + 8, y + 4);
    display_draw_line(x - 8, y + 4, x - 4, y + 1);
    display_draw_line(x - 4, y + 1, x + 4, y + 1);
    display_draw_line(x + 4, y + 1, x + 8, y + 4);
    display_draw_circle(x, y + 6, 1, false);
}

static void display_draw_skull(int x, int y)
{
    display_draw_circle(x, y + 5, 6, false);
    display_draw_rect(x - 5, y + 8, 10, 4);
    display_draw_circle(x - 2, y + 5, 1, false);
    display_draw_circle(x + 2, y + 5, 1, false);
}

static void display_draw_driftwood(int x, int y)
{
    display_draw_line(x - 8, y + 10, x + 8, y + 6);
    display_draw_line(x - 2, y + 8, x - 6, y + 3);
    display_draw_line(x + 1, y + 8, x + 5, y + 2);
    display_draw_line(x + 4, y + 7, x + 9, y + 4);
}

static void display_draw_bridge(int x, int y)
{
    // Bolder silhouette: thick deck, peaked arch, and heavy posts.
    display_draw_line(x - 9, y + 10, x + 9, y + 10);  // deck top
    display_draw_line(x - 9, y + 11, x + 9, y + 11);  // deck thickness

    display_draw_line(x - 9, y + 10, x - 5, y + 7);   // arch left outer
    display_draw_line(x - 5, y + 7, x, y + 5);        // arch apex left
    display_draw_line(x, y + 5, x + 5, y + 7);        // arch apex right
    display_draw_line(x + 5, y + 7, x + 9, y + 10);   // arch right outer

    display_draw_line(x - 6, y + 10, x - 2, y + 7);   // arch left inner
    display_draw_line(x - 2, y + 7, x + 2, y + 7);    // arch crown inner
    display_draw_line(x + 2, y + 7, x + 6, y + 10);   // arch right inner

    display_draw_line(x - 9, y + 11, x - 9, y + 6);   // left post
    display_draw_line(x + 9, y + 11, x + 9, y + 6);   // right post
    display_draw_line(x - 4, y + 11, x - 4, y + 8);   // center-left support
    display_draw_line(x + 4, y + 11, x + 4, y + 8);   // center-right support
}

static void display_draw_anchor(int x, int y)
{
    // Classic anchor silhouette: ring, shank, stock, arms, and flukes.
    display_draw_circle(x, y + 1, 2, false);          // ring
    display_draw_line(x, y + 3, x, y + 12);           // shank
    display_draw_line(x - 4, y + 5, x + 4, y + 5);    // stock
    display_draw_line(x - 6, y + 10, x + 6, y + 10);  // arm bar
    display_draw_line(x - 6, y + 10, x - 8, y + 12);  // left arm
    display_draw_line(x + 6, y + 10, x + 8, y + 12);  // right arm
    display_draw_line(x - 8, y + 12, x - 6, y + 12);  // left fluke
    display_draw_line(x + 8, y + 12, x + 6, y + 12);  // right fluke
}

static void display_draw_moai_head(int x, int y)
{
    // Front-facing carved face with clear eyes, nose, lips, and jaw.
    display_draw_rect(x - 5, y + 1, 10, 12);         // head block
    display_draw_line(x - 5, y + 3, x + 5, y + 3);   // heavy brow
    display_set_pixel(x - 2, y + 5, true);           // left eye
    display_set_pixel(x + 2, y + 5, true);           // right eye
    display_draw_line(x, y + 4, x, y + 9);           // nose bridge
    display_draw_line(x - 1, y + 8, x + 1, y + 8);   // nose base
    display_draw_line(x - 2, y + 10, x + 2, y + 10); // upper lip
    display_draw_line(x - 1, y + 11, x + 1, y + 11); // lower lip
    display_draw_line(x - 3, y + 12, x + 3, y + 12); // jaw line
    display_set_pixel(x - 4, y + 6, true);           // left cheek chip
    display_set_pixel(x + 4, y + 7, true);           // right cheek chip
}

static void display_draw_volcano(int x, int y)
{
    display_draw_line(x - 8, y + 12, x - 2, y + 3);
    display_draw_line(x + 8, y + 12, x + 2, y + 3);
    display_draw_line(x - 2, y + 3, x + 2, y + 3);
    display_draw_line(x - 1, y + 1, x + 1, y - 1);
    display_draw_line(x, y + 1, x + 2, y - 2);
}

static void display_draw_column(int x, int y)
{
    display_draw_rect(x - 5, y + 1, 10, 2);
    display_draw_rect(x - 3, y + 3, 6, 8);
    display_draw_rect(x - 6, y + 11, 12, 2);
}

static void display_draw_diver_helmet(int x, int y)
{
    display_draw_circle(x, y + 7, 6, false);
    display_draw_rect(x - 3, y + 5, 6, 4);
    display_draw_rect(x - 2, y + 12, 4, 2);
}

static void display_draw_pvc_pipe(int x, int y)
{
    display_draw_rect(x - 7, y + 7, 8, 4);
    display_draw_rect(x - 1, y + 3, 4, 8);
}

static void display_draw_seashell(int x, int y)
{
    display_draw_line(x - 7, y + 12, x, y + 4);
    display_draw_line(x, y + 4, x + 7, y + 12);
    display_draw_line(x - 7, y + 12, x + 7, y + 12);
    display_draw_line(x - 4, y + 11, x - 2, y + 7);
    display_draw_line(x, y + 11, x, y + 6);
    display_draw_line(x + 4, y + 11, x + 2, y + 7);
}

static void display_draw_terracotta_pot(int x, int y)
{
    display_draw_rect(x - 6, y + 4, 12, 2);
    display_draw_line(x - 5, y + 6, x - 2, y + 12);
    display_draw_line(x + 5, y + 6, x + 2, y + 12);
    display_draw_line(x - 2, y + 12, x + 2, y + 12);
}

static void display_draw_starfish(int x, int y)
{
    // 5-point starfish silhouette with a compact center.
    display_draw_line(x, y + 4, x - 2, y + 8);      // upper-left arm
    display_draw_line(x, y + 4, x + 2, y + 8);      // upper-right arm
    display_draw_line(x - 2, y + 8, x - 6, y + 9);  // left arm
    display_draw_line(x + 2, y + 8, x + 6, y + 9);  // right arm
    display_draw_line(x - 1, y + 10, x - 3, y + 13);// lower-left arm
    display_draw_line(x + 1, y + 10, x + 3, y + 13);// lower-right arm
    display_draw_line(x - 2, y + 8, x + 2, y + 8);  // center top
    display_draw_line(x - 1, y + 9, x + 1, y + 9);  // center middle
    display_draw_line(x - 1, y + 10, x + 1, y + 10);// center bottom
}

static void display_draw_sea_urchin(int x, int y)
{
    // Spiky urchin body with radial quills.
    display_draw_circle(x, y + 9, 2, false);
    display_draw_line(x, y + 4, x, y + 14);
    display_draw_line(x - 5, y + 7, x + 5, y + 11);
    display_draw_line(x - 4, y + 12, x + 4, y + 6);
    display_draw_line(x + 4, y + 12, x - 4, y + 6);
}

static void display_draw_shark_slide(int x, int y)
{
    // Strong shark profile for OLED readability: open mouth, fins, and forked tail.
    display_draw_line(x - 10, y + 9, x - 4, y + 5);  // snout/top jaw
    display_draw_line(x - 10, y + 9, x - 4, y + 12); // lower jaw
    display_draw_line(x - 4, y + 5, x + 8, y + 7);   // back/top body
    display_draw_line(x - 4, y + 12, x + 8, y + 11); // belly/bottom body

    display_draw_line(x - 1, y + 6, x + 2, y + 2);   // dorsal fin front
    display_draw_line(x + 2, y + 2, x + 5, y + 7);   // dorsal fin rear

    display_draw_line(x + 1, y + 10, x + 4, y + 12); // pectoral fin

    display_draw_line(x + 8, y + 7, x + 11, y + 4);  // tail upper
    display_draw_line(x + 8, y + 11, x + 11, y + 14);// tail lower
    display_draw_line(x + 8, y + 7, x + 8, y + 11);  // tail root

    display_set_pixel(x - 6, y + 8, true);           // eye
    display_draw_line(x - 5, y + 8, x - 5, y + 10);  // gill slit

    // Teeth hint in the mouth opening.
    display_set_pixel(x - 8, y + 9, true);
    display_set_pixel(x - 7, y + 9, true);
    display_set_pixel(x - 6, y + 10, true);

    // Keep simple ladder supports under the slide body.
    display_draw_line(x + 2, y + 11, x + 2, y + 14);
    display_draw_line(x + 5, y + 11, x + 5, y + 14);
    display_draw_line(x + 2, y + 13, x + 5, y + 13);
}

static void display_draw_sword(int x, int y)
{
    display_draw_line(x, y + 3, x, y + 12);
    display_draw_line(x - 1, y + 4, x + 1, y + 4);
    display_draw_line(x - 3, y + 8, x + 3, y + 8);
    display_draw_line(x - 6, y + 12, x + 6, y + 12);
}

static void display_draw_lotus(int x, int y)
{
    // Layered petals with a lily pad base for a cleaner lotus silhouette.
    display_draw_line(x, y + 6, x, y + 12);          // stem
    display_draw_line(x, y + 5, x - 2, y + 8);       // top petal left
    display_draw_line(x, y + 5, x + 2, y + 8);       // top petal right
    display_draw_line(x - 5, y + 9, x - 1, y + 7);   // outer petal left
    display_draw_line(x + 5, y + 9, x + 1, y + 7);   // outer petal right
    display_draw_line(x - 3, y + 11, x, y + 8);      // lower petal left
    display_draw_line(x + 3, y + 11, x, y + 8);      // lower petal right
    display_draw_line(x - 6, y + 12, x + 6, y + 12); // lily pad top
    display_draw_line(x - 5, y + 13, x + 5, y + 13); // lily pad bottom
    display_draw_line(x, y + 12, x + 2, y + 13);     // pad notch
}

static void display_draw_barrel(int x, int y)
{
    display_draw_rect(x - 5, y + 5, 10, 7);
    display_draw_line(x - 5, y + 7, x + 5, y + 7);
    display_draw_line(x - 5, y + 10, x + 5, y + 10);
}

static void display_draw_pineapple_house(int x, int y)
{
    display_draw_circle(x, y + 9, 5, false);
    display_draw_line(x - 2, y + 3, x, y);
    display_draw_line(x, y, x + 2, y + 3);
    display_draw_line(x - 4, y + 4, x - 1, y + 1);
    display_draw_line(x + 4, y + 4, x + 1, y + 1);
    display_draw_rect(x - 2, y + 9, 4, 3);
}

static void display_draw_eiffel_tower(int x, int y)
{
    display_draw_line(x, y + 1, x, y + 13);
    display_draw_line(x - 6, y + 13, x, y + 2);
    display_draw_line(x + 6, y + 13, x, y + 2);
    display_draw_line(x - 4, y + 9, x + 4, y + 9);
    display_draw_line(x - 2, y + 6, x + 2, y + 6);
    display_draw_line(x - 7, y + 13, x + 7, y + 13);
}

static void display_draw_great_wall(int x, int y)
{
    display_draw_line(x - 9, y + 11, x - 5, y + 9);
    display_draw_line(x - 5, y + 9, x - 1, y + 10);
    display_draw_line(x - 1, y + 10, x + 3, y + 8);
    display_draw_line(x + 3, y + 8, x + 9, y + 9);
    display_draw_line(x - 9, y + 13, x + 9, y + 13);

    // Crenellations along the top edge to read as fortified wall segments.
    display_draw_line(x - 8, y + 10, x - 8, y + 9);
    display_draw_line(x - 6, y + 10, x - 6, y + 9);
    display_draw_line(x - 2, y + 11, x - 2, y + 10);
    display_draw_line(x, y + 10, x, y + 9);
    display_draw_line(x + 4, y + 9, x + 4, y + 8);
    display_draw_line(x + 6, y + 9, x + 6, y + 8);

    display_draw_line(x - 8, y + 11, x - 8, y + 13);
    display_draw_line(x - 2, y + 10, x - 2, y + 13);
    display_draw_line(x + 4, y + 8, x + 4, y + 13);
}

static void display_draw_pyramids_giza(int x, int y)
{
    display_draw_line(x - 9, y + 13, x - 2, y + 4);
    display_draw_line(x - 2, y + 4, x + 5, y + 13);
    display_draw_line(x - 6, y + 13, x + 1, y + 5);
    display_draw_line(x + 1, y + 5, x + 8, y + 13);
    display_draw_line(x - 10, y + 13, x + 10, y + 13);
}

static void display_draw_statue_of_liberty(int x, int y)
{
    display_draw_rect(x - 3, y + 8, 6, 5);
    display_draw_line(x - 2, y + 8, x, y + 3);
    display_draw_line(x, y + 3, x + 2, y + 8);
    display_draw_line(x + 1, y + 4, x + 4, y + 2);
    display_set_pixel(x + 4, y + 1, true);
    display_draw_line(x - 5, y + 13, x + 5, y + 13);
}

static void display_draw_taj_mahal(int x, int y)
{
    display_draw_rect(x - 7, y + 8, 14, 5);
    display_draw_line(x - 4, y + 8, x, y + 4);
    display_draw_line(x, y + 4, x + 4, y + 8);
    display_draw_rect(x - 1, y + 9, 2, 4);
    display_draw_line(x - 9, y + 13, x + 9, y + 13);
    display_draw_line(x - 8, y + 8, x - 8, y + 5);
    display_draw_line(x + 8, y + 8, x + 8, y + 5);
}

static void display_draw_colosseum(int x, int y)
{
    display_draw_rect(x - 8, y + 7, 16, 6);
    display_draw_line(x - 8, y + 7, x - 5, y + 4);
    display_draw_line(x + 8, y + 7, x + 5, y + 4);
    for (int px = -6; px <= 6; px += 4) {
        display_draw_rect(x + px - 1, y + 9, 2, 3);
    }
}

static void display_draw_sydney_opera_house(int x, int y)
{
    display_draw_line(x - 8, y + 12, x + 8, y + 12);
    display_draw_line(x - 6, y + 12, x - 2, y + 6);
    display_draw_line(x - 2, y + 6, x, y + 12);
    display_draw_line(x - 1, y + 12, x + 3, y + 5);
    display_draw_line(x + 3, y + 5, x + 5, y + 12);
}

static void display_draw_burj_khalifa(int x, int y)
{
    display_draw_line(x, y + 2, x, y + 13);
    display_draw_line(x - 2, y + 13, x + 2, y + 13);
    display_draw_line(x - 1, y + 10, x + 1, y + 10);
    display_draw_line(x - 1, y + 7, x + 1, y + 7);
    display_set_pixel(x, y + 1, true);
}

static void display_draw_christ_redeemer(int x, int y)
{
    // Human-forward silhouette: head, shoulders, robe body, and arms.
    display_draw_circle(x, y + 2, 1, false);          // head
    display_draw_line(x - 2, y + 4, x + 2, y + 4);    // shoulders
    display_draw_line(x, y + 4, x, y + 12);           // torso center
    display_draw_line(x - 2, y + 4, x - 3, y + 11);   // robe left edge
    display_draw_line(x + 2, y + 4, x + 3, y + 11);   // robe right edge
    display_draw_line(x - 3, y + 11, x + 3, y + 11);  // robe hem

    display_draw_line(x - 8, y + 7, x - 2, y + 6);    // left arm
    display_draw_line(x + 2, y + 6, x + 8, y + 7);    // right arm
    display_set_pixel(x - 8, y + 7, true);            // left hand
    display_set_pixel(x + 8, y + 7, true);            // right hand

    display_draw_line(x - 9, y + 13, x + 9, y + 13);  // mountain/base
}

static void display_draw_leaning_tower_pisa(int x, int y)
{
    display_draw_line(x - 2, y + 4, x - 1, y + 13);
    display_draw_line(x + 2, y + 4, x + 3, y + 13);
    display_draw_line(x - 3, y + 13, x + 4, y + 13);
    display_draw_line(x - 2, y + 10, x + 3, y + 10);
    display_draw_line(x - 2, y + 7, x + 2, y + 7);
}

static void display_draw_stonehenge(int x, int y)
{
    display_draw_line(x - 6, y + 8, x - 6, y + 13);
    display_draw_line(x, y + 8, x, y + 13);
    display_draw_line(x + 6, y + 8, x + 6, y + 13);
    display_draw_line(x - 7, y + 8, x + 1, y + 8);
    display_draw_line(x - 1, y + 8, x + 7, y + 8);
}

static void display_draw_big_ben(int x, int y)
{
    display_draw_rect(x - 3, y + 3, 6, 10);
    display_draw_rect(x - 4, y + 2, 8, 2);
    display_draw_circle(x, y + 7, 2, false);
    display_draw_line(x, y + 7, x, y + 6);
    display_draw_line(x, y + 7, x + 1, y + 7);
}

static void display_draw_golden_gate_bridge(int x, int y)
{
    // Deck
    display_draw_line(x - 10, y + 13, x + 10, y + 13);
    display_draw_line(x - 10, y + 12, x + 10, y + 12);

    // Two tall towers
    display_draw_line(x - 6, y + 4, x - 6, y + 13);
    display_draw_line(x - 4, y + 4, x - 4, y + 13);
    display_draw_line(x + 4, y + 4, x + 4, y + 13);
    display_draw_line(x + 6, y + 4, x + 6, y + 13);
    display_draw_line(x - 6, y + 4, x - 4, y + 4);
    display_draw_line(x + 4, y + 4, x + 6, y + 4);

    // Main suspension cable (segmented curve)
    display_draw_line(x - 10, y + 8, x - 6, y + 4);
    display_draw_line(x - 6, y + 4, x - 1, y + 7);
    display_draw_line(x - 1, y + 7, x + 1, y + 7);
    display_draw_line(x + 1, y + 7, x + 6, y + 4);
    display_draw_line(x + 6, y + 4, x + 10, y + 8);

    // Vertical hangers down to deck
    display_draw_line(x - 8, y + 6, x - 8, y + 12);
    display_draw_line(x - 3, y + 6, x - 3, y + 12);
    display_draw_line(x, y + 7, x, y + 12);
    display_draw_line(x + 3, y + 6, x + 3, y + 12);
    display_draw_line(x + 8, y + 6, x + 8, y + 12);
}

static void display_draw_empire_state_building(int x, int y)
{
    display_draw_rect(x - 4, y + 4, 8, 9);
    display_draw_rect(x - 2, y + 2, 4, 2);
    display_draw_line(x, y + 1, x, y + 2);
    display_draw_line(x - 3, y + 7, x + 3, y + 7);
    display_draw_line(x - 3, y + 10, x + 3, y + 10);
}

static void display_draw_cloud_gate_bean(int x, int y)
{
    // Bean-like reflective arch silhouette.
    display_draw_line(x - 7, y + 9, x - 5, y + 6);
    display_draw_line(x - 5, y + 6, x - 1, y + 5);
    display_draw_line(x - 1, y + 5, x + 3, y + 6);
    display_draw_line(x + 3, y + 6, x + 7, y + 9);
    display_draw_line(x - 7, y + 9, x - 5, y + 12);
    display_draw_line(x + 7, y + 9, x + 5, y + 12);
    display_draw_line(x - 5, y + 12, x + 5, y + 12);
    display_draw_line(x - 2, y + 11, x + 2, y + 11);
    display_set_pixel(x - 3, y + 8, true);
    display_set_pixel(x + 2, y + 8, true);
}

static void display_draw_pot_of_gold(int x, int y)
{
    // Pot body - wider trapezoid shape
    display_draw_line(x - 5, y + 9, x + 5, y + 9);   // bottom
    display_draw_line(x - 3, y + 5, x + 3, y + 5);   // top opening
    display_draw_line(x - 5, y + 9, x - 3, y + 5);   // left side
    display_draw_line(x + 5, y + 9, x + 3, y + 5);   // right side
    // Pot rim
    display_draw_line(x - 4, y + 5, x + 4, y + 5);
    // Handles - larger and more pronounced
    display_draw_line(x - 3, y + 5, x - 5, y + 3);   // left handle
    display_draw_line(x - 5, y + 3, x - 4, y + 2);
    display_draw_line(x + 3, y + 5, x + 5, y + 3);   // right handle
    display_draw_line(x + 5, y + 3, x + 4, y + 2);
    // Gold coins cascading out
    display_set_pixel(x - 6, y + 8, true);
    display_set_pixel(x - 6, y + 6, true);
    display_set_pixel(x - 5, y + 4, true);
    display_set_pixel(x + 6, y + 8, true);
    display_set_pixel(x + 6, y + 6, true);
    display_set_pixel(x + 5, y + 4, true);
}

static void display_draw_coral_fan(int x, int y)
{
    // Thicker stem
    display_draw_line(x - 1, y + 13, x - 1, y + 9);
    display_draw_line(x + 1, y + 13, x + 1, y + 9);
    display_draw_line(x - 1, y + 13, x + 1, y + 13);
    // Fan branches radiating upward and outward - more branches
    display_draw_line(x, y + 9, x - 6, y + 3);
    display_draw_line(x, y + 9, x - 3, y + 2);
    display_draw_line(x, y + 9, x, y + 1);
    display_draw_line(x, y + 9, x + 3, y + 2);
    display_draw_line(x, y + 9, x + 6, y + 3);
    // Additional branch layers for depth
    display_draw_line(x - 1, y + 9, x - 4, y + 3);
    display_draw_line(x + 1, y + 9, x + 4, y + 3);
    // Branch tips
    display_set_pixel(x - 7, y + 2, true);
    display_set_pixel(x - 4, y + 1, true);
    display_set_pixel(x + 4, y + 1, true);
    display_set_pixel(x + 7, y + 2, true);
}

static void display_draw_drift_bottle_note(int x, int y)
{
    // Bottle body - wider and taller
    display_draw_line(x - 3, y + 7, x - 4, y + 13);  // left side
    display_draw_line(x + 3, y + 7, x + 4, y + 13);  // right side
    display_draw_line(x - 4, y + 13, x + 4, y + 13); // bottom
    display_draw_line(x - 3, y + 7, x + 3, y + 7);   // body width
    // Bottle neck - more defined
    display_draw_line(x - 2, y + 7, x - 1, y + 4);
    display_draw_line(x + 2, y + 7, x + 1, y + 4);
    // Cork/stopper - bigger
    display_draw_line(x - 2, y + 3, x + 2, y + 3);
    display_draw_line(x - 2, y + 2, x + 2, y + 2);
    display_draw_line(x - 2, y + 1, x + 2, y + 1);
    // Message visible through glass
    display_set_pixel(x - 2, y + 10, true);
    display_set_pixel(x, y + 11, true);
    display_set_pixel(x + 2, y + 10, true);
    display_set_pixel(x - 1, y + 9, true);
    display_set_pixel(x + 1, y + 9, true);
}

static void display_draw_mini_sub_wreck(int x, int y)
{
    // Hull - horizontal line, wider
    display_draw_line(x - 7, y + 10, x + 7, y + 10);
    // Submarine body outline
    display_draw_line(x - 7, y + 9, x - 7, y + 11);
    display_draw_line(x + 7, y + 9, x + 7, y + 11);
    display_draw_line(x - 6, y + 8, x + 6, y + 8);
    // Conning tower (top) - bigger
    display_draw_line(x - 2, y + 8, x + 2, y + 8);
    display_draw_line(x - 2, y + 8, x - 2, y + 5);
    display_draw_line(x + 2, y + 8, x + 2, y + 5);
    display_draw_line(x - 2, y + 5, x + 2, y + 5);
    // Periscope
    display_draw_line(x, y + 5, x, y + 2);
    // Viewing ports
    display_draw_circle(x - 4, y + 10, 1, false);
    display_draw_circle(x + 4, y + 10, 1, false);
    // Propeller hint
    display_set_pixel(x - 1, y + 11, true);
    display_set_pixel(x + 1, y + 11, true);
}

static void display_draw_amphora_jar(int x, int y)
{
    // Top rim opening - wider
    display_draw_line(x - 3, y + 3, x + 3, y + 3);
    display_draw_line(x - 3, y + 2, x + 3, y + 2);
    // Neck - tapered
    display_draw_line(x - 3, y + 3, x - 3, y + 6);
    display_draw_line(x + 3, y + 3, x + 3, y + 6);
    // Shoulder bulge
    display_draw_line(x - 3, y + 6, x - 5, y + 8);
    display_draw_line(x + 3, y + 6, x + 5, y + 8);
    // Main body (rounded) - bigger
    display_draw_line(x - 5, y + 8, x - 5, y + 12);
    display_draw_line(x + 5, y + 8, x + 5, y + 12);
    // Bottom curve
    display_draw_line(x - 5, y + 12, x - 3, y + 13);
    display_draw_line(x + 5, y + 12, x + 3, y + 13);
    display_draw_line(x - 3, y + 13, x + 3, y + 13);
    // Handles - larger looping handles
    display_draw_line(x - 3, y + 6, x - 6, y + 7);
    display_draw_line(x - 6, y + 7, x - 6, y + 10);
    display_draw_line(x + 3, y + 6, x + 6, y + 7);
    display_draw_line(x + 6, y + 7, x + 6, y + 10);
}

static void display_draw_sunken_ship_wheel(int x, int y)
{
    // Wheel rim - larger circle with thickness
    display_draw_circle(x, y + 9, 6, false);
    display_draw_circle(x, y + 9, 5, false);
    // Hub center - bigger
    display_draw_circle(x, y + 9, 2, true);
    // Four main spokes - longer
    display_draw_line(x, y + 3, x, y + 15);  // vertical
    display_draw_line(x - 6, y + 9, x + 6, y + 9);  // horizontal
    display_draw_line(x - 4, y + 5, x + 4, y + 13);  // diagonal
    display_draw_line(x + 4, y + 5, x - 4, y + 13);  // diagonal
    // Spoke details
    display_draw_line(x - 1, y + 8, x - 1, y + 10);
    display_draw_line(x + 1, y + 8, x + 1, y + 10);
}

static void display_draw_day_sun_rays(void)
{
    if (!aquarium_should_show_sun_rays()) {
        sun_ray_origin_x = -1;
        sun_ray_beam_count = 0;
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (sun_ray_origin_x < 0) {
        sun_ray_origin_x = (int)(esp_random() % SCREEN_WIDTH);
        sun_ray_beam_count = 1 + (int)(esp_random() % 3);
    }

    int sway = (int)((now / pdMS_TO_TICKS(1400)) % 3) - 1;
    int top_x0 = clamp_int(sun_ray_origin_x - 3 + sway, 0, SCREEN_WIDTH - 1);
    int top_x1 = clamp_int(sun_ray_origin_x + sway, 0, SCREEN_WIDTH - 1);
    int top_x2 = clamp_int(sun_ray_origin_x + 3 + sway, 0, SCREEN_WIDTH - 1);

    int bottom_x0 = clamp_int(top_x0 + 16, 0, SCREEN_WIDTH - 1);
    int bottom_x1 = clamp_int(top_x1 + 17, 0, SCREEN_WIDTH - 1);
    int bottom_x2 = clamp_int(top_x2 + 18, 0, SCREEN_WIDTH - 1);

    // Long diagonal beams that start at the top and land on the sand line.
    // Keep the number sparse so the tank doesn't get flooded with light.
    if (sun_ray_beam_count >= 1) {
        display_draw_dotted_line(top_x0, 0, bottom_x0, SCREEN_HEIGHT - 1);
    }
    if (sun_ray_beam_count >= 2) {
        display_draw_dotted_line(top_x1, 0, bottom_x1, SCREEN_HEIGHT - 1);
    }
    if (sun_ray_beam_count >= 3) {
        display_draw_dotted_line(top_x2, 0, bottom_x2, SCREEN_HEIGHT - 1);
    }
}

static void display_draw_dotted_line(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int error = dx - dy;
    int step = 0;

    while (true) {
        if ((step % 3) != 1) {
            display_set_pixel(x1, y1, true);
        }
        if (x1 == x2 && y1 == y2) {
            break;
        }

        int error2 = 2 * error;
        if (error2 > -dy) {
            error -= dy;
            x1 += sx;
        }
        if (error2 < dx) {
            error += dx;
            y1 += sy;
        }
        step++;
    }
}

static void display_draw_night_moon(void)
{
    if (aquarium_is_daytime()) {
        return;
    }

    // Draw a larger crescent moon at (8,7): outer circle (radius 4), inner circle (radius 3, offset right)
    int cx = 8, cy = 3;
    int outer_r = 4;
    int inner_r = 3;
    for (int dy = -outer_r; dy <= outer_r; dy++) {
        for (int dx = -outer_r; dx <= outer_r; dx++) {
            int ox = cx + dx;
            int oy = cy + dy;
            if (dx*dx + dy*dy <= outer_r*outer_r) {
                // Offset the inner circle more to the right for a clean crescent
                int inner_dx = dx - 2;
                int inner_dy = dy;
                // Only draw if outside the inner circle (to make a crescent)
                if (inner_dx*inner_dx + inner_dy*inner_dy >= inner_r*inner_r) {
                    // Remove stray dot by not drawing the rightmost column
                    if (dx < outer_r) {
                        display_set_pixel(ox, oy, true);
                    }
                }
            }
        }
    }
}

static void display_draw_rain_surface(void)
{
    int frame = game_state.time_elapsed;
    const int ripple_y = WATER_TOP - 1;

    if (!aquarium_is_raining()) {
        for (int i = 0; i < (int)(sizeof(rain_drops) / sizeof(rain_drops[0])); i++) {
            rain_drops[i].active = false;
        }
        last_rain_frame = frame;
        return;
    }

    if (last_rain_frame != frame) {
        last_rain_frame = frame;

        int spawn_chance = 38;
        for (int i = 0; i < (int)(sizeof(rain_drops) / sizeof(rain_drops[0])); i++) {
            rain_drop_t *drop = &rain_drops[i];

            if (!drop->active) {
                if ((esp_random() % 100) < spawn_chance) {
                    drop->active = true;
                    drop->state = 0;
                    drop->age = 0;
                    drop->x = 2 + (esp_random() % (SCREEN_WIDTH - 4));
                    drop->y = esp_random() % 2;
                    drop->speed = 1 + (esp_random() % 2);
                }
                continue;
            }

            if (drop->state == 0) {
                drop->y += drop->speed;
                if (drop->y >= ripple_y - 1) {
                    drop->y = ripple_y;
                    drop->state = 1;
                    drop->age = 0;
                }
            } else {
                drop->age++;
                if (drop->age > 3) {
                    drop->active = false;
                }
            }
        }
    }

    for (int i = 0; i < (int)(sizeof(rain_drops) / sizeof(rain_drops[0])); i++) {
        const rain_drop_t *drop = &rain_drops[i];
        if (!drop->active) {
            continue;
        }

        if (drop->state == 0) {
            display_set_pixel(drop->x, drop->y, true);
            if (drop->y > 0 && ((drop->x + frame) & 1) == 0) {
                display_set_pixel(drop->x, drop->y - 1, true);
            }
            continue;
        }

        if (drop->age == 0) {
            display_set_pixel(drop->x, ripple_y, true);
        } else if (drop->age == 1) {
            display_set_pixel(drop->x - 1, ripple_y, true);
            display_set_pixel(drop->x + 1, ripple_y, true);
        } else if (drop->age == 2) {
            display_set_pixel(drop->x - 2, ripple_y, true);
            display_set_pixel(drop->x + 2, ripple_y, true);
            display_set_pixel(drop->x - 1, ripple_y + 1, true);
            display_set_pixel(drop->x + 1, ripple_y + 1, true);
        } else {
            display_set_pixel(drop->x - 3, ripple_y, true);
            display_set_pixel(drop->x + 3, ripple_y, true);
            display_set_pixel(drop->x - 2, ripple_y + 1, true);
            display_set_pixel(drop->x + 2, ripple_y + 1, true);
        }
    }
}

static void display_draw_mini_lighthouse(int x, int y)
{
    // Tower base - wider and taller, clearly tapering
    display_draw_line(x - 4, y + 13, x + 4, y + 13);  // bottom
    display_draw_line(x - 4, y + 13, x - 3, y + 11);
    display_draw_line(x + 4, y + 13, x + 3, y + 11);
    display_draw_line(x - 3, y + 11, x - 3, y + 8);
    display_draw_line(x + 3, y + 11, x + 3, y + 8);
    display_draw_line(x - 2, y + 8, x - 2, y + 6);
    display_draw_line(x + 2, y + 8, x + 2, y + 6);
    // Lantern room - wider top section
    display_draw_line(x - 3, y + 6, x + 3, y + 6);
    display_draw_line(x - 3, y + 6, x - 4, y + 4);
    display_draw_line(x + 3, y + 6, x + 4, y + 4);
    // Lantern (beacon) - domed top
    display_draw_line(x - 4, y + 4, x + 4, y + 4);
    display_draw_line(x - 3, y + 3, x + 3, y + 3);
    display_draw_line(x - 2, y + 2, x + 2, y + 2);
    display_set_pixel(x, y + 1, true);
    // Light rays
    display_set_pixel(x - 4, y + 3, true);
    display_set_pixel(x + 4, y + 3, true);
}

static void display_draw_lobster(int x, int y)
{
    // Head/body section
    display_draw_circle(x, y + 7, 2, false);
    
    // Body segments (thorax + abdomen)
    display_draw_circle(x, y + 10, 2, false);
    display_draw_line(x - 2, y + 10, x - 2, y + 12);
    display_draw_line(x + 2, y + 10, x + 2, y + 12);
    
    // Tail section
    display_draw_circle(x, y + 13, 1, false);
    
    // Left claw - upper arm and pincer
    display_draw_line(x - 2, y + 8, x - 6, y + 6);
    display_draw_line(x - 6, y + 6, x - 7, y + 4);
    display_draw_line(x - 7, y + 4, x - 6, y + 3);
    
    // Right claw - upper arm and pincer
    display_draw_line(x + 2, y + 8, x + 6, y + 6);
    display_draw_line(x + 6, y + 6, x + 7, y + 4);
    display_draw_line(x + 7, y + 4, x + 6, y + 3);
    
    // Left antennae
    display_draw_line(x - 1, y + 5, x - 3, y + 2);
    
    // Right antennae
    display_draw_line(x + 1, y + 5, x + 3, y + 2);
}

static void display_draw_clam_pearl(int x, int y)
{
    // Left shell half - bigger
    display_draw_line(x - 7, y + 4, x - 7, y + 9);
    display_draw_line(x - 7, y + 9, x - 3, y + 11);
    display_draw_line(x - 3, y + 11, x, y + 10);
    display_draw_line(x, y + 10, x - 7, y + 4);
    // Shell ridge pattern
    display_draw_line(x - 6, y + 5, x - 5, y + 5);
    display_draw_line(x - 6, y + 7, x - 5, y + 7);
    display_draw_line(x - 5, y + 9, x - 4, y + 9);
    // Right shell half - bigger
    display_draw_line(x + 7, y + 4, x + 7, y + 9);
    display_draw_line(x + 7, y + 9, x + 3, y + 11);
    display_draw_line(x + 3, y + 11, x, y + 10);
    display_draw_line(x, y + 10, x + 7, y + 4);
    // Shell ridge pattern
    display_draw_line(x + 6, y + 5, x + 5, y + 5);
    display_draw_line(x + 6, y + 7, x + 5, y + 7);
    display_draw_line(x + 5, y + 9, x + 4, y + 9);
    // Pearl inside - bigger and more visible
    display_draw_circle(x, y + 7, 2, false);
    display_draw_circle(x, y + 7, 1, false);
    display_set_pixel(x - 1, y + 8, true);
    display_set_pixel(x + 1, y + 8, true);
}

static void display_draw_jellyfish(int x, int y)
{
    // Bell body - larger rounded top
    display_draw_circle(x, y + 5, 4, false);
    display_draw_circle(x, y + 6, 3, false);
    // Bell outline reinforcement
    display_draw_line(x - 4, y + 7, x - 3, y + 9);
    display_draw_line(x + 4, y + 7, x + 3, y + 9);
    display_draw_line(x - 3, y + 9, x + 3, y + 9);
    // Trailing tentacles - 7 total, more visible
    display_draw_line(x - 4, y + 9, x - 5, y + 13);
    display_draw_line(x - 2, y + 9, x - 3, y + 13);
    display_draw_line(x, y + 9, x, y + 13);
    display_draw_line(x + 2, y + 9, x + 3, y + 13);
    display_draw_line(x + 4, y + 9, x + 5, y + 13);
    // Extra wavy tentacles for depth
    display_draw_line(x - 3, y + 9, x - 4, y + 11);
    display_draw_line(x + 3, y + 9, x + 4, y + 11);
}

static void display_draw_poseidon_trident(int x, int y)
{
    // Handle/shaft - thicker
    display_draw_line(x - 1, y + 4, x - 1, y + 13);
    display_draw_line(x + 1, y + 4, x + 1, y + 13);
    display_draw_line(x - 1, y + 13, x + 1, y + 13);
    // Guard/crossbar - wider
    display_draw_line(x - 5, y + 7, x + 5, y + 7);
    display_draw_line(x - 5, y + 8, x + 5, y + 8);
    // Left prong - longer
    display_draw_line(x - 5, y + 7, x - 6, y + 3);
    display_draw_line(x - 6, y + 3, x - 7, y + 1);
    display_draw_line(x - 5, y + 8, x - 6, y + 5);
    // Center prong - taller
    display_draw_line(x, y + 7, x, y + 2);
    display_draw_line(x - 1, y + 7, x, y + 2);
    // Right prong - longer
    display_draw_line(x + 5, y + 7, x + 6, y + 3);
    display_draw_line(x + 6, y + 3, x + 7, y + 1);
    display_draw_line(x + 5, y + 8, x + 6, y + 5);
    // Decorative grip lines on handle
    display_draw_line(x - 1, y + 10, x + 1, y + 10);
    display_draw_line(x - 1, y + 12, x + 1, y + 12);
}

static void display_draw_decoration(decor_kind_t kind, int x, int y)
{
    switch (kind) {
        case DECOR_POT_OF_GOLD:
            display_draw_pot_of_gold(x, y - 12);
            break;
        case DECOR_CORAL_FAN:
            display_draw_coral_fan(x, y - 14);
            break;
        case DECOR_DRIFT_BOTTLE_NOTE:
            display_draw_drift_bottle_note(x, y - 14);
            break;
        case DECOR_MINI_SUB_WRECK:
            display_draw_mini_sub_wreck(x, y - 14);
            break;
        case DECOR_AMPHORA_JAR:
            display_draw_amphora_jar(x, y - 14);
            break;
        case DECOR_SUNKEN_SHIP_WHEEL:
            display_draw_sunken_ship_wheel(x, y - 14);
            break;
        case DECOR_MINI_LIGHTHOUSE:
            display_draw_mini_lighthouse(x, y - 14);
            break;
        case DECOR_LOBSTER:
            display_draw_lobster(x, y - 14);
            break;
        case DECOR_CLAM_PEARL:
            display_draw_clam_pearl(x, y - 12);
            break;
        case DECOR_JELLYFISH:
            display_draw_jellyfish(x, y - 14);
            break;
        case DECOR_POSEIDON_TRIDENT:
            display_draw_poseidon_trident(x, y - 14);
            break;
        case DECOR_SEAWEED:
            display_draw_seaweed(x, y - 14);
            break;
        case DECOR_CASTLE:
            display_draw_castle(x, y - 14);
            break;
        case DECOR_SHIPWRECK:
            display_draw_shipwreck(x, y - 12);
            break;
        case DECOR_TREASURE_CHEST:
            display_draw_treasure_chest(x, y - 12);
            break;
        case DECOR_SKULL:
            display_draw_skull(x, y - 14);
            break;
        case DECOR_DRIFTWOOD:
            display_draw_driftwood(x, y - 14);
            break;
        case DECOR_BRIDGE:
            display_draw_bridge(x, y - 14);
            break;
        case DECOR_ANCHOR:
            display_draw_anchor(x, y - 14);
            break;
        case DECOR_MOAI_HEAD:
            display_draw_moai_head(x, y - 14);
            break;
        case DECOR_VOLCANO:
            display_draw_volcano(x, y - 14);
            break;
        case DECOR_COLUMN:
            display_draw_column(x, y - 14);
            break;
        case DECOR_DIVER_HELMET:
            display_draw_diver_helmet(x, y - 14);
            break;
        case DECOR_PVC_PIPE:
            display_draw_pvc_pipe(x, y - 14);
            break;
        case DECOR_SEASHELL:
            display_draw_seashell(x, y - 14);
            break;
        case DECOR_TERRACOTTA_POT:
            display_draw_terracotta_pot(x, y - 14);
            break;
        case DECOR_STARFISH:
            display_draw_starfish(x, y - 14);
            break;
        case DECOR_SEA_URCHIN:
            display_draw_sea_urchin(x, y - 14);
            break;
        case DECOR_SHARK_SLIDE:
            display_draw_shark_slide(x, y - 14);
            break;
        case DECOR_SWORD:
            display_draw_sword(x, y - 14);
            break;
        case DECOR_LOTUS:
            display_draw_lotus(x, y - 14);
            break;
        case DECOR_BARREL:
            display_draw_barrel(x, y - 14);
            break;
        case DECOR_PINEAPPLE_HOUSE:
            display_draw_pineapple_house(x, y - 14);
            break;
        case DECOR_EIFFEL_TOWER:
            display_draw_eiffel_tower(x, y - 14);
            break;
        case DECOR_GREAT_WALL:
            display_draw_great_wall(x, y - 14);
            break;
        case DECOR_PYRAMIDS_GIZA:
            display_draw_pyramids_giza(x, y - 14);
            break;
        case DECOR_STATUE_OF_LIBERTY:
            display_draw_statue_of_liberty(x, y - 14);
            break;
        case DECOR_TAJ_MAHAL:
            display_draw_taj_mahal(x, y - 14);
            break;
        case DECOR_COLOSSEUM:
            display_draw_colosseum(x, y - 14);
            break;
        case DECOR_SYDNEY_OPERA_HOUSE:
            display_draw_sydney_opera_house(x, y - 14);
            break;
        case DECOR_BURJ_KHALIFA:
            display_draw_burj_khalifa(x, y - 14);
            break;
        case DECOR_CHRIST_REDEEMER:
            display_draw_christ_redeemer(x, y - 14);
            break;
        case DECOR_LEANING_TOWER_PISA:
            display_draw_leaning_tower_pisa(x, y - 14);
            break;
        case DECOR_STONEHENGE:
            display_draw_stonehenge(x, y - 14);
            break;
        case DECOR_BIG_BEN:
            display_draw_big_ben(x, y - 14);
            break;
        case DECOR_GOLDEN_GATE_BRIDGE:
            display_draw_golden_gate_bridge(x, y - 14);
            break;
        case DECOR_EMPIRE_STATE_BUILDING:
            display_draw_empire_state_building(x, y - 14);
            break;
        case DECOR_CLOUD_GATE_BEAN:
            display_draw_cloud_gate_bean(x, y - 14);
            break;
        default:
            display_draw_pot_of_gold(x, y - 12);
            break;
    }
}

static void display_draw_present(int x, int y, bool shaking, int shake_speed)
{
    static int shake_phase = 0;
    static const int8_t pattern_x[8] = {0, 1, -1, 2, -2, 1, -1, 0};
    static const int8_t pattern_y[8] = {0, -1, 1, -2, 2, 1, -1, 0};

    if (shake_speed < 1) {
        shake_speed = 1;
    }
    if (shake_speed > 9) {
        shake_speed = 9;
    }

    if (shaking) {
        shake_phase = (shake_phase + shake_speed) & 7;
        int amplitude = 1 + (shake_speed / 3);
        x += (pattern_x[shake_phase] * amplitude) / 2;
        y += (pattern_y[shake_phase] * amplitude) / 2;
    }

    display_draw_line(x - 10, y - 8, x + 10, y - 8);
    display_draw_line(x - 10, y - 8, x - 10, y + 8);
    display_draw_line(x + 10, y - 8, x + 10, y + 8);
    display_draw_line(x - 10, y + 8, x + 10, y + 8);
    display_draw_line(x, y - 8, x, y + 8);
    display_draw_line(x - 10, y, x + 10, y);
    display_draw_line(x - 3, y - 11, x + 3, y - 5);
    display_draw_line(x - 3, y - 5, x + 3, y - 11);
}

static void display_draw_can_tab_icon(int x, int y)
{
    // More realistic pull-tab: elongated ring, finger hole, rivet, and tongue.
    int left = x - 7;
    int right = x + 7;
    int top = y - 4;
    int bottom = y + 4;

    // Outer tab body (oval / racetrack shape).
    display_draw_circle(left, y, 4, false);
    display_draw_circle(right, y, 4, false);
    display_draw_line(left, top, right, top);
    display_draw_line(left, bottom, right, bottom);

    // Finger hole in the ring.
    display_draw_circle(x - 2, y, 2, false);

    // Central rivet and bridge area.
    display_draw_circle(x + 4, y - 1, 1, true);
    display_draw_line(x + 1, y - 1, x + 5, y - 1);
    display_draw_line(x + 1, y, x + 5, y);

    // Score/tongue section at the front of the tab.
    display_fill_rect(x + 5, y - 1, 3, 2, true);
    display_draw_line(x + 5, y + 2, x + 8, y + 2);
}

static void display_render(void)
{
    display_send_command(0x21);
    display_send_command(0);
    display_send_command(127);
    display_send_command(0x22);
    display_send_command(0);
    display_send_command(7);
    display_send_data(display_buffer, sizeof(display_buffer));
}

void display_power_off(void)
{
    display_send_command(0xAE);
}

void display_power_on(void)
{
    display_send_command(0xAF);
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing SSD1306 display...");

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SSD1306_SDA_GPIO,
        .scl_io_num = SSD1306_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    i2c_param_config(SSD1306_I2C_PORT, &conf);
    i2c_driver_install(SSD1306_I2C_PORT, conf.mode, 0, 0, 0);

    display_send_command(0xAE);
    display_send_command(0xD5);
    display_send_command(0x80);
    display_send_command(0xA8);
    display_send_command(0x3F);
    display_send_command(0xD3);
    display_send_command(0x00);
    display_send_command(0x40);
    display_send_command(0x8D);
    display_send_command(0x14);
    display_send_command(0x20);
    display_send_command(0x00);
    display_send_command(0xA1);
    display_send_command(0xC8);
    display_send_command(0xDA);
    display_send_command(0x12);
    display_send_command(0x81);
    display_send_command(0xCF);
    display_send_command(0xD9);
    display_send_command(0xF1);
    display_send_command(0xDB);
    display_send_command(0x40);
    display_send_command(0xAF);

    display_clear();
    display_render();
}

void display_draw_aquarium_screen(int currency, const char *status, bool show_clock_mode, const char *clock_text, int mode_badge)
{
    (void)currency;
    (void)status;
    const int decor_y_offset = 17;
    const int bottom_line_y = SCREEN_HEIGHT - 2;

    display_clear();

    for (int x = 0; x < SCREEN_WIDTH; x += 4) {
        display_set_pixel(x, bottom_line_y, true);
    }

    display_draw_day_sun_rays();
    display_draw_night_moon();
    display_draw_rain_surface();

    if (show_clock_mode) {
        const char *text = (clock_text != NULL && clock_text[0] != '\0') ? clock_text : "--:--";
        const int scale = 4;
        const int text_w = (int)strlen(text) * 6 * scale;
        const int text_h = 7 * scale;
        int text_x = (SCREEN_WIDTH - text_w) / 2;
        int text_y = (SCREEN_HEIGHT - text_h) / 2;
        if (text_x < 0) {
            text_x = 0;
        }
        if (text_y < 0) {
            text_y = 0;
        }

        display_draw_scaled_text(text_x, text_y, text, scale);
    }

    if (mode_badge == 1) {
        display_draw_text(SCREEN_WIDTH - 6, 0, "W");
    } else if (mode_badge == 2) {
        display_draw_text(SCREEN_WIDTH - 6, 0, "C");
    }

    // Snail slime residue: short-lived marks that thin out before disappearing.
    for (int i = 0; i < MAX_SLIME; i++) {
        if (!slime_list[i].active) {
            continue;
        }

        if (slime_list[i].lifetime_frames > 50) {
            display_set_pixel(slime_list[i].x, slime_list[i].y, true);
            display_set_pixel(slime_list[i].x + 1, slime_list[i].y, true);
        } else {
            display_set_pixel(slime_list[i].x, slime_list[i].y, true);
        }
    }

    for (int i = 0; i < NUM_FISH; i++) {
        if (fish_tank[i].alive) {
            const fish_species_t *species = fish_catalog_get(fish_tank[i].species_index);
            const char *name = (species != NULL) ? species->name : "";

            if (strcmp(name, "Snail") == 0) {
                display_draw_snail(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].vx, fish_tank[i].vy);
            } else if (strcmp(name, "Crab") == 0) {
                display_draw_crab(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Shark") == 0) {
                display_draw_shark(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Angelfish") == 0) {
                display_draw_angelfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Catfish") == 0) {
                display_draw_catfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Silver Arowana") == 0) {
                display_draw_arowana(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Piranhas") == 0) {
                display_draw_piranha(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Pufferfish") == 0) {
                display_draw_pufferfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Hatchetfish") == 0) {
                display_draw_hatchetfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Lionfish") == 0) {
                display_draw_lionfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else if (strcmp(name, "Clownfish") == 0) {
                display_draw_clownfish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            } else {
                display_draw_fish(fish_tank[i].x, fish_tank[i].y, fish_tank[i].size, fish_tank[i].facing_right);
            }
        }
    }

    for (int i = 0; i < MAX_DECOR; i++) {
        if (!decor_list[i].active || !decor_list[i].visible) {
            continue;
        }

        display_draw_decoration_bottom_aligned(decor_list[i].kind, decor_list[i].x, decor_list[i].y + decor_y_offset);
    }

    for (int i = 0; i < MAX_BUBBLES; i++) {
        if (!bubble_list[i].active) {
            continue;
        }

        display_draw_circle(bubble_list[i].x, bubble_list[i].y, bubble_list[i].radius, false);
    }

    for (int i = 0; i < MAX_FOOD; i++) {
        if (food_list[i].active) {
            int radius = food_list[i].size;
            if (radius < 1) {
                radius = 1;
            } else if (radius > 2) {
                radius = 2;
            }
            display_draw_circle(food_list[i].x, food_list[i].y, radius, true);
        }
    }

    for (int i = 0; i < MAX_SLEEP_Z; i++) {
        if (!sleep_z_list[i].active) {
            continue;
        }

        display_draw_tiny_sleep_z(sleep_z_list[i].x, sleep_z_list[i].y);
    }

    if (aquarium_should_flash_thunder()) {
        display_invert_rect(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    display_render();
}

void display_draw_blindbox_screen(int tabs, bool shaking, int shake_speed, const char *prize_name, const char *prize_desc, bool show_prize, bool show_tab_reward)
{
    display_clear();

    display_draw_text(0, 0, "Tabquarium");
    char tabs_text[24];
    snprintf(tabs_text, sizeof(tabs_text), "TABS %d", tabs);
    int tabs_text_w = ((int)strlen(tabs_text) * 6) - 1;
    int tabs_text_x = SCREEN_WIDTH - tabs_text_w;
    if (tabs_text_x < 0) {
        tabs_text_x = 0;
    }
    display_draw_text(tabs_text_x, 0, tabs_text);

    display_draw_present(SCREEN_WIDTH / 2, 28, shaking, shake_speed);

    int fill_count = (tabs >= 3) ? 3 : tabs;

    for (int i = 0; i < 3; i++) {
        bool filled = i < fill_count;
        display_draw_circle(44 + (i * 20), 52, 5, filled);
    }

    if (show_prize) {
        display_fill_rect(2, 14, 123, 30, false);
        display_draw_rect(2, 14, 123, 30);
        display_draw_text(6, 18, prize_name != NULL ? prize_name : "PRIZE");
        display_draw_text(6, 30, prize_desc != NULL ? prize_desc : "");
    } else if (show_tab_reward) {
        display_fill_rect(18, 14, 92, 32, false);
        display_draw_rect(18, 14, 92, 32);
        display_draw_can_tab_icon(36, 29);
        display_draw_text(56, 22, "TAB");
        display_draw_text(56, 32, "EARNED");
    }

    display_render();
}

void display_draw_clock_set_screen(const char *clock_text)
{
    display_clear();

    const char *text = (clock_text != NULL && clock_text[0] != '\0') ? clock_text : "--:--";
    const int scale = 4;
    const int text_w = (int)strlen(text) * 6 * scale;
    const int text_h = 7 * scale;
    int text_x = (SCREEN_WIDTH - text_w) / 2;
    int text_y = 18;
    if (text_x < 0) {
        text_x = 0;
    }

    display_draw_scaled_text(text_x, text_y, text, scale);
    display_draw_text(2, 0, "SET CLOCK");
    display_draw_text(2, 56, "A+MIN B+HR");

    display_render();
}

void display_draw_fish_screen(int scroll_offset, bool show_sell_prompt, int sell_price, bool sell_confirm_pending)
{
    const int visible_rows = 5;
    const int text_left = 1;
    const int text_area_width = SCREEN_WIDTH - text_left;
    display_clear();

    display_draw_text(0, 0, "FISH");

    int alive_indices[NUM_FISH];
    int alive_count = 0;
    for (int i = 0; i < NUM_FISH; i++) {
        if (fish_tank[i].alive) {
            alive_indices[alive_count++] = i;
        }
    }

    char header[20];
    snprintf(header, sizeof(header), "FISH: %d", alive_count);
    display_draw_text(0, 0, header);

    if (alive_count == 0) {
        display_draw_text(0, 18, "NO FISH YET");
        display_render();
        return;
    }

    int selected_index = scroll_offset;
    if (selected_index < 0) {
        selected_index = 0;
    }
    if (selected_index >= alive_count) {
        selected_index = alive_count - 1;
    }

    int rows_to_draw = (alive_count < visible_rows) ? alive_count : visible_rows;
    int max_start = alive_count - visible_rows;
    if (max_start < 0) {
        max_start = 0;
    }

    int start = selected_index;
    if (start > max_start) {
        start = max_start;
    }

    int selected_row = selected_index - start;
    if (selected_row < 0) {
        selected_row = 0;
    }
    if (selected_row >= rows_to_draw) {
        selected_row = rows_to_draw - 1;
    }

    for (int row = 0; row < rows_to_draw; row++) {
        int fish_index = alive_indices[start + row];
        const char *name = fish_names_get(fish_tank[fish_index].name_index);
        const fish_species_t *species = fish_catalog_get(fish_tank[fish_index].species_index);
        const char *species_name = (species != NULL) ? species->name : "Unknown";
        int whole_length = fish_tank[fish_index].length_tenths / 10;
        int frac_length = fish_tank[fish_index].length_tenths % 10;
        char line[48];
        snprintf(line, sizeof(line), "%s, %s, %d.%d\"", name, species_name, whole_length, frac_length);

        int text_x = text_left;
        if (row == selected_row) {
            int text_width = ((int)strlen(line) * 6) - 1;
            if (text_width > text_area_width) {
                int overflow = text_width - text_area_width;
                int hold_ticks = 12;
                int period = (overflow * 2) + (hold_ticks * 2);
                int phase = (int)((xTaskGetTickCount() / 2) % period);
                int scroll_px;

                if (phase < hold_ticks) {
                    scroll_px = 0;
                } else if (phase < hold_ticks + overflow) {
                    scroll_px = phase - hold_ticks;
                } else if (phase < hold_ticks + overflow + hold_ticks) {
                    scroll_px = overflow;
                } else {
                    scroll_px = overflow - (phase - (hold_ticks + overflow + hold_ticks));
                }

                text_x = text_left - scroll_px;
            }
        }

        display_draw_text(text_x, 12 + (row * 10), line);
    }

    if (rows_to_draw > 0) {
        display_highlight_row(11 + (selected_row * 10));
    }

    if (show_sell_prompt) {
        display_fill_rect(4, 14, 119, 40, false);
        display_draw_rect(4, 14, 119, 40);
        display_draw_text(10, 18, "SELL FISH?");
        char price_line[24];
        snprintf(price_line, sizeof(price_line), "%d TABS", sell_price);
        display_draw_text(10, 28, price_line);
        if (!sell_confirm_pending) {
            display_draw_text(10, 38, "RELEASE BUTTON");
        } else {
            display_draw_text(10, 38, "A SELL  B CANCEL");
        }
    }

    display_render();
}

void display_draw_decorations_screen(int scroll_offset)
{
    const int visible_rows = 5;
    const int text_left = 1;
    const int text_area_width = SCREEN_WIDTH - text_left;
    decor_kind_t owned_kinds[DECOR_KIND_COUNT];
    int owned_count = collect_owned_decor_kinds(owned_kinds, DECOR_KIND_COUNT);

    display_clear();
    char header[20];
    snprintf(header, sizeof(header), "DECOR: %d", owned_count);
    display_draw_text(0, 0, header);
    display_draw_text(98, 0, "MAX 4");

    if (owned_count == 0) {
        display_draw_text(0, 18, "NO DECOR YET");
        display_render();
        return;
    }

    int selected_index = scroll_offset;
    if (selected_index < 0) {
        selected_index = 0;
    }
    if (selected_index >= owned_count) {
        selected_index = owned_count - 1;
    }

    int rows_to_draw = (owned_count < visible_rows) ? owned_count : visible_rows;
    int max_start = owned_count - visible_rows;
    if (max_start < 0) {
        max_start = 0;
    }

    int start = selected_index;
    if (start > max_start) {
        start = max_start;
    }

    int selected_row = selected_index - start;
    if (selected_row < 0) {
        selected_row = 0;
    }
    if (selected_row >= rows_to_draw) {
        selected_row = rows_to_draw - 1;
    }

    for (int row = 0; row < rows_to_draw; row++) {
        decor_kind_t kind = owned_kinds[start + row];
        const char *name = aquarium_decor_name(kind);
        bool visible = aquarium_is_decor_visible(kind);
        char line[32];
        snprintf(line, sizeof(line), "%c %s", visible ? '*' : ' ', name);

        int text_x = text_left;
        if (row == selected_row) {
            int text_width = ((int)strlen(line) * 6) - 1;
            if (text_width > text_area_width) {
                int overflow = text_width - text_area_width;
                int hold_ticks = 12;
                int period = (overflow * 2) + (hold_ticks * 2);
                int phase = (int)((xTaskGetTickCount() / 2) % period);
                int scroll_px;

                if (phase < hold_ticks) {
                    scroll_px = 0;
                } else if (phase < hold_ticks + overflow) {
                    scroll_px = phase - hold_ticks;
                } else if (phase < hold_ticks + overflow + hold_ticks) {
                    scroll_px = overflow;
                } else {
                    scroll_px = overflow - (phase - (hold_ticks + overflow + hold_ticks));
                }

                text_x = text_left - scroll_px;
            }
        }

        display_draw_text(text_x, 12 + (row * 10), line);
    }

    if (rows_to_draw > 0) {
        display_highlight_row(11 + (selected_row * 10));
    }

    display_render();
}
