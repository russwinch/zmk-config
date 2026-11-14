/*
 * See status_overlay.h. Positions and colours are the Field layout's
 * (prospector-zmk-module, src/layouts/field/battery_label.c and output.c),
 * with its output widget moved from under the layer name to the bottom
 * right corner.
 *
 * A mask byte is a colour number (high four bits) and how solid the pixel is
 * (low four: the fonts have 16 levels). Colour 0 is the shadow that keeps
 * the text readable over a bright picture.
 */

#include "status_overlay.h"

#include <stdbool.h>
#include <string.h>

#include <lvgl.h>

LV_FONT_DECLARE(FR_Regular_30);
LV_FONT_DECLARE(FG_Medium_26);
LV_FONT_DECLARE(Symbols_Regular_28);
LV_FONT_DECLARE(Symbols_Bold_26);

#define SYMBOL_WAVES_UP 0x102667 /* wireless */

#define PANEL_W (FX_W * 2)
#define BATTERY_X 6
#define OUTPUT_X (PANEL_W - 5 - OVERLAY_OUTPUT_W)

/* The Field screen's: text bottom at 20 above the screen's, 9 from its side. */
#define BATTERY_LEFT 9
#define OUTPUT_BOX (PANEL_W - 4 - 68) /* the Field output box's left edge */
#define LINE_BOTTOM 220

enum {
    SHADOW,
    WHITE,        /* NUM_ACTIVE, and the batteries */
    GREY,         /* NUM_INACTIVE */
    SYM_SENDING,  /* connected */
    SYM_SEARCHING,
    SYM_UNPAIRED,
};

/* RGB565 */
static const uint16_t colors[] = {
    [WHITE] = 0xffff,         /* ffffff */
    [GREY] = 0xa514,          /* a0a0a0 */
    [SYM_SENDING] = 0x07ff,   /* 00ffff */
    [SYM_SEARCHING] = 0xd69a, /* d0d0d0 */
    [SYM_UNPAIRED] = 0x4228,  /* 454545 */
};

static uint8_t *battery_mask, *output_mask;

void overlay_set_memory(uint8_t *memory) {
    battery_mask = memory;
    output_mask = memory + OVERLAY_BATTERY_W * OVERLAY_ROWS;
}

struct area {
    uint8_t *mask;
    int x, width; /* on the panel */
};

static void put(const struct area *area, int x, int y, uint8_t color, uint8_t alpha) {
    x -= area->x;
    y -= OVERLAY_TOP;
    if (alpha == 0 || (unsigned)x >= (unsigned)area->width || (unsigned)y >= OVERLAY_ROWS) {
        return;
    }
    uint8_t *m = &area->mask[y * area->width + x];
    if ((*m & 0x0f) < alpha) {
        *m = (uint8_t)(color << 4 | alpha);
    }
}

/* One character, its line box's top left at (x, top). Returns how far to
 * move on for the next one. */
static int draw_glyph(const struct area *area, const lv_font_t *font, uint32_t letter, uint32_t next,
                      int x, int top, uint8_t color, bool draw) {
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_get_glyph_dsc(font, &dsc, letter, next)) {
        return 0;
    }
    if (!draw || dsc.format != LV_FONT_GLYPH_FORMAT_A4) {
        return dsc.adv_w;
    }
    /* the font's own data: 4 bits a pixel, rows run on without padding */
    lv_draw_buf_t unused = {0};
    dsc.req_raw_bitmap = 1;
    const uint8_t *bits = lv_font_get_glyph_bitmap(&dsc, &unused);
    if (bits != NULL) {
        const int gx = x + dsc.ofs_x;
        const int gy = top + font->line_height - font->base_line - dsc.box_h - dsc.ofs_y;
        int i = 0;
        for (int row = 0; row < dsc.box_h; row++) {
            for (int col = 0; col < dsc.box_w; col++, i++) {
                put(area, gx + col, gy + row, color, i & 1 ? bits[i / 2] & 0x0f : bits[i / 2] >> 4);
            }
        }
    }
    return dsc.adv_w;
}

/* Text with its line box's top left at (x, top); returns its width. */
static int draw_text(const struct area *area, const lv_font_t *font, const char *s, int x, int top,
                     int letter_space, uint8_t color, bool draw) {
    int width = 0;
    for (; *s; s++) {
        width += draw_glyph(area, font, (uint8_t)s[0], (uint8_t)s[1], x + width, top, color, draw);
        if (s[1]) {
            width += letter_space;
        }
    }
    return width;
}

/* Darken around the text: full up to two pixels from it, half at three. */
static void add_shadow(const struct area *area) {
    for (int y = 0; y < OVERLAY_ROWS; y++) {
        for (int x = 0; x < area->width; x++) {
            uint8_t *m = &area->mask[y * area->width + x];
            if (*m >> 4) {
                continue;
            }
            int shade = 0;
            for (int dy = -3; dy <= 3; dy++) {
                for (int dx = -3; dx <= 3; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if ((unsigned)nx >= (unsigned)area->width || (unsigned)ny >= OVERLAY_ROWS) {
                        continue;
                    }
                    const uint8_t n = area->mask[ny * area->width + nx];
                    if (n >> 4) {
                        const int far = dx * dx + dy * dy;
                        const int a = far <= 5 ? n & 0x0f : far <= 10 ? (n & 0x0f) / 2 : 0;
                        shade = a > shade ? a : shade;
                    }
                }
            }
            *m = (uint8_t)shade;
        }
    }
}

static char *put_level(char *p, uint8_t percent, bool connected) {
    if (!connected) {
        *p++ = '-';
        return p;
    }
    if (percent >= 100) {
        *p++ = '1';
        percent %= 100;
        *p++ = '0' + percent / 10;
    } else if (percent >= 10) {
        *p++ = '0' + percent / 10;
    }
    *p++ = '0' + percent % 10;
    return p;
}

void overlay_render(const struct fx_status *status) {
    const struct area battery = {battery_mask, BATTERY_X, OVERLAY_BATTERY_W};
    const struct area output = {output_mask, OUTPUT_X, OVERLAY_OUTPUT_W};
    memset(battery_mask, 0, OVERLAY_BYTES);

    /* "85/80", "-" for a half that isn't connected */
    char levels[10], *p = levels;
    p = put_level(p, status->battery[0], status->half_connected[0]);
    *p++ = '/';
    p = put_level(p, status->battery[1], status->half_connected[1]);
    *p = '\0';
    draw_text(&battery, &FR_Regular_30, levels, BATTERY_LEFT, LINE_BOTTOM - FR_Regular_30.line_height, -1,
              WHITE, true);
    add_shadow(&battery);

    /* The output, as the Field screen has it: nothing over USB; on Bluetooth
     * the waves and the profile number, laid out as in its 68 x 34 box (the
     * symbol in the middle of a 30 wide space 8 in, the number 5 from the
     * right), with that box in the bottom right corner. */
    if (status->usb) {
        return;
    }
    const bool solid = status->host_connected;
    const lv_font_t *symbols = solid ? &Symbols_Bold_26 : &Symbols_Regular_28;
    const uint8_t symbol_color = solid ? SYM_SENDING : status->host_open ? SYM_UNPAIRED : SYM_SEARCHING;
    /* both symbol fonts end up on the same baseline, one above the number's */
    const int symbol_top = LINE_BOTTOM - 7 - (symbols->line_height - symbols->base_line);
    const int symbol_wide = draw_glyph(&output, symbols, SYMBOL_WAVES_UP, 0, 0, 0, 0, false);
    draw_glyph(&output, symbols, SYMBOL_WAVES_UP, 0, OUTPUT_BOX + 8 + (30 - symbol_wide) / 2, symbol_top,
               symbol_color, true);

    const char number[2] = {(char)('0' + status->profile % 10), '\0'};
    const int number_top = LINE_BOTTOM - 6 - (FG_Medium_26.line_height - FG_Medium_26.base_line);
    const int number_wide = draw_text(&output, &FG_Medium_26, number, 0, 0, 0, 0, false);
    draw_text(&output, &FG_Medium_26, number, OUTPUT_BOX + 68 - 5 - number_wide, number_top, 0,
              solid ? WHITE : GREY, true);
    add_shadow(&output);
}

static inline uint16_t swap16(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }

static void blend(uint16_t *pixels, const uint8_t *mask, int count) {
    for (int i = 0; i < count; i++) {
        const uint8_t m = mask[i];
        if (m == 0) {
            continue;
        }
        const int alpha = m & 0x0f, color = m >> 4;
        const uint16_t under = swap16(pixels[i]);
        int r = under >> 11, g = (under >> 5) & 0x3f, b = under & 0x1f;
        /* the shadow takes the picture down to a fifth at most */
        const int keep = color ? 3 : 16 - alpha * 13 / 15;
        r = r * keep / 16;
        g = g * keep / 16;
        b = b * keep / 16;
        if (color) {
            const uint16_t c = colors[color];
            r += ((c >> 11) - r) * alpha / 15;
            g += (((c >> 5) & 0x3f) - g) * alpha / 15;
            b += ((c & 0x1f) - b) * alpha / 15;
        }
        pixels[i] = swap16((uint16_t)(r << 11 | g << 5 | b));
    }
}

void overlay_blend_row(uint16_t *pixels, int x, int y, int count) {
    y -= OVERLAY_TOP;
    if ((unsigned)y >= OVERLAY_ROWS) {
        return;
    }
    const struct area areas[] = {
        {battery_mask, BATTERY_X, OVERLAY_BATTERY_W},
        {output_mask, OUTPUT_X, OVERLAY_OUTPUT_W},
    };
    for (int a = 0; a < 2; a++) {
        const int from = x > areas[a].x ? x : areas[a].x;
        const int end_a = areas[a].x + areas[a].width, end_p = x + count;
        const int to = end_p < end_a ? end_p : end_a;
        if (from < to) {
            blend(&pixels[from - x], &areas[a].mask[y * areas[a].width + from - areas[a].x], to - from);
        }
    }
}
