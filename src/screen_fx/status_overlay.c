/*
 * See status_overlay.h. The fonts, sizes and the way the output is laid out
 * are the Field layout's (prospector-zmk-module, src/layouts/field/
 * battery_label.c and output.c), except that a theme can ask for the numbers
 * in the animations' own letters.
 *
 * A mask byte is a colour number (high four bits) and how solid the pixel is
 * (low four: the fonts have 16 levels). Colour 0 is the halo around the text
 * that keeps it readable over the picture: the theme's halo colour, black
 * for light text and something light for dark text.
 *
 * Each of the four masks (OVERLAY_LEFT...) is mixed in at its own level,
 * halo and all: that is how the symbol pulses, a flat battery flashes and
 * the profile number fades away.
 */

#include "status_overlay.h"

#include <string.h>

#include <lvgl.h>

LV_FONT_DECLARE(FR_Regular_36);
LV_FONT_DECLARE(FG_Medium_26);
LV_FONT_DECLARE(Symbols_Regular_28);
LV_FONT_DECLARE(Symbols_Bold_26);

#define SYMBOL_WAVES_UP 0x102667 /* wireless */

#define PANEL_W (FX_W * 2)
#define LEFT_X 6
#define RIGHT_X (PANEL_W - LEFT_X - OVERLAY_BATTERY_W)
/* the symbol in the middle of the panel, the number beside it */
#define SYMBOL_X ((PANEL_W - OVERLAY_SYMBOL_W) / 2)
#define NUMBER_X (SYMBOL_X + OVERLAY_SYMBOL_W - 2)

/* The Field screen's: text bottom at 20 above the screen's, 9 from its side. */
#define SIDE 9
#define FULL 16        /* a mask's level when it is all there */
#define PULSE_LOW 5    /* the least of the searching symbol */
#define SHOWN_MS 2000  /* how long what fades stays first */
#define FADE_MS 600
#define LINE_BOTTOM 220
#define BATTERY_W 24 /* with its cap */
#define BATTERY_H 13
#define PIXEL_SCALE 3 /* FX_FONT_PIXEL: panel pixels to a dot (the animations' own text has 2) */

enum {
    HALO,
    INK,      /* the batteries, and the profile number */
    DIM,      /* the profile number when the computer isn't connected */
    ACCENT,   /* the symbol, connected */
    LOW,      /* a battery at FX_LOW_BATTERY or under */
    WARN,     /* a battery at FX_WARN_BATTERY or under */
    SEARCH,   /* the symbol, not connected */
    COLORS,
};

/* RGB565 */
static uint16_t colors[COLORS];
static uint8_t levels[OVERLAY_AREAS]; /* 0..FULL */
static struct fx_status drawn;        /* what the masks show */
static bool have_drawn, stale;        /* stale: the theme has changed since */
static bool revealed;                 /* the output is being shown for a change to it... */
static uint32_t revealed_at;          /* ...since then */
static bool pixel_font, dots;
static int drop, inset; /* panel rows down, and columns in from the sides */

int overlay_frame_row0(void) { return (OVERLAY_TOP + drop) / 2; }
int overlay_frame_row1(void) { return (OVERLAY_TOP + drop + OVERLAY_ROWS - 1) / 2; }

void overlay_set_theme(const struct fx_theme *theme) {
    pixel_font = theme->font == FX_FONT_PIXEL;
    dots = theme->dots;
    /* no lower than the panel */
    drop = theme->drop < FX_H * 2 - OVERLAY_TOP - OVERLAY_ROWS ? theme->drop : FX_H * 2 - OVERLAY_TOP - OVERLAY_ROWS;
    /* in from the sides too: the panel's corners are round */
    inset = drop * 2 / 3;
    colors[HALO] = theme->halo;
    colors[INK] = theme->ink;
    colors[DIM] = theme->dim;
    colors[ACCENT] = theme->accent;
    colors[LOW] = theme->low;
    colors[WARN] = 0xFDE5; /* amber: 255, 190, 40 */
    colors[SEARCH] = theme->search;
    stale = true;
}

/* The searching symbol breathes once a second: down to about a third and
 * back, eased at both ends. */
static int pulse_at(uint32_t ms) {
    const int t = (int)(ms % 1000), up = t < 500 ? t : 1000 - t; /* 0..500 */
    const int eased = up * up * (1500 - 2 * up) / 250000;         /* 0..500, smoothstep */
    return PULSE_LOW + eased * (FULL - PULSE_LOW) / 500;
}

static uint8_t *masks;

void overlay_set_memory(uint8_t *memory) {
    masks = memory;
    stale = true;
}

struct area {
    uint8_t *mask;
    int x, width; /* on the panel */
};

static struct area area_of(int which) {
    switch (which) {
    case OVERLAY_LEFT:
        return (struct area){masks, LEFT_X + inset, OVERLAY_BATTERY_W};
    case OVERLAY_RIGHT:
        return (struct area){masks + OVERLAY_BATTERY_W * OVERLAY_ROWS, RIGHT_X - inset, OVERLAY_BATTERY_W};
    case OVERLAY_SYMBOL:
        return (struct area){masks + 2 * OVERLAY_BATTERY_W * OVERLAY_ROWS, SYMBOL_X, OVERLAY_SYMBOL_W};
    default:
        return (struct area){masks + (2 * OVERLAY_BATTERY_W + OVERLAY_SYMBOL_W) * OVERLAY_ROWS, NUMBER_X,
                             OVERLAY_NUMBER_W};
    }
}

void overlay_area_span(int which, int *x, int *width) {
    const struct area area = area_of(which);
    *x = area.x;
    *width = area.width;
}

static void put(const struct area *area, int x, int y, uint8_t color, uint8_t alpha) {
    x -= area->x;
    y -= OVERLAY_TOP + drop;
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

/* The same in the animations' 5x7 letters, `scale` panel pixels to a dot,
 * the top of the letters at `top`. */
static int draw_pixel_text(const struct area *area, const char *s, int x, int top, int scale, uint8_t color,
                           bool draw) {
    int width = 0;
    for (; *s; s++, width += 5 * scale) {
        const uint8_t *rows = fx_glyph_rows(*s);
        if (!draw || rows == NULL) {
            continue;
        }
        for (int row = 0; row < 7 * scale; row++) {
            for (int col = 0; col < 5 * scale; col++) {
                if (rows[row / scale] & (0x10 >> col / scale)) {
                    put(area, x + width + col, top + row, color, 15);
                }
            }
        }
    }
    return width - scale; /* less the last letter's gap */
}

/* The halo around the text: full up to two pixels from it, half at three. */
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

/* A battery BATTERY_W by BATTERY_H, its top left at (x, y) and its cap on
 * the right, as full as `percent`. */
static void draw_battery(const struct area *area, int x, int y, uint8_t percent, uint8_t color) {
    const int body = BATTERY_W - 3, room = body - 8;
    const int full = 2 + (percent > 100 ? 100 : percent) * (room - 2) / 100;
    for (int dy = 0; dy < BATTERY_H; dy++) {
        for (int dx = 0; dx < BATTERY_W; dx++) {
            const bool edge = dx < body && (dx < 2 || dx >= body - 2 || dy < 2 || dy >= BATTERY_H - 2);
            const bool cap = dx >= body && dy >= 4 && dy < BATTERY_H - 4;
            const bool inside = dx >= 4 && dx < 4 + full && dy >= 4 && dy < BATTERY_H - 4;
            if (edge || cap || inside) {
                put(area, x + dx, y + dy, color, 15);
            }
        }
    }
}

/* A battery level, "-" when its half isn't connected or hasn't said, a bolt
 * when it is on USB power. */
static uint8_t level_text(char *p, uint8_t percent, bool connected, bool charging) {
    if (connected && charging) {
        strcpy(p, FX_BOLT); /* the level is the charger's voltage then */
        return INK;
    }
    if (!connected || percent == 0) {
        strcpy(p, "-");
        return INK;
    }
    if (percent >= 100) {
        strcpy(p, "100");
        return INK;
    }
    if (percent >= 10) {
        *p++ = '0' + percent / 10;
    }
    *p++ = '0' + percent % 10;
    *p = '\0';
    return percent <= FX_LOW_BATTERY ? LOW : percent <= FX_WARN_BATTERY ? WARN : INK;
}

/* Work the masks out. */
static void render(const struct fx_status *status) {
    const struct area left = area_of(OVERLAY_LEFT), right = area_of(OVERLAY_RIGHT);
    const struct area symbol = area_of(OVERLAY_SYMBOL), number_area = area_of(OVERLAY_NUMBER);
    memset(masks, 0, OVERLAY_BYTES);

    /* everything stands on the line the Field screen's smaller levels do */
    const int line_bottom = LINE_BOTTOM + drop, foot = line_bottom - 5;
    const lv_font_t *font = &FR_Regular_36;
    const int top = foot - (font->line_height - font->base_line);
    const int pixel_top = foot - 6 * PIXEL_SCALE; /* the digits leave the 7th row empty */
    char level[4];
    for (int half = 0; half < 2; half++) {
        const struct area *area = half ? &right : &left;
        const uint8_t color =
            level_text(level, status->battery[half], status->half_connected[half], status->charging[half]);
        if (dots) {
            /* where the level's outer digit would be, at half its height */
            if (color != INK) {
                const int x = half ? PANEL_W - SIDE - inset - BATTERY_W : SIDE + inset;
                draw_battery(area, x, foot - (pixel_font ? 9 : 12) - BATTERY_H / 2, status->battery[half], color);
                add_shadow(area);
            }
            continue;
        }
        /* the bolt is only in the pixel letters: a size up for the Field font */
        const bool pixels = pixel_font || level[0] == FX_GLYPH_BOLT;
        const int scale = pixel_font ? PIXEL_SCALE : PIXEL_SCALE + 1;
        const int wide = pixels ? draw_pixel_text(area, level, 0, 0, scale, 0, false)
                                : draw_text(area, font, level, 0, 0, -1, 0, false);
        const int x = half ? PANEL_W - SIDE - inset - wide : SIDE + inset;
        if (pixels) {
            draw_pixel_text(area, level, x, foot - 6 * scale, scale, color, true);
        } else {
            draw_text(area, font, level, x, top, -1, color, true);
        }
        add_shadow(area);
    }

    /* The output: the Field screen's waves in the middle of the panel, and
     * the Bluetooth profile number to their right. Solid when that profile's
     * computer is connected. Not on a screensaver. */
    if (dots) {
        return;
    }
    const bool solid = status->usb ? status->profile_connected : status->host_connected;
    const lv_font_t *symbols = solid ? &Symbols_Bold_26 : &Symbols_Regular_28;
    /* both symbol fonts end up on the same baseline, one above the number's */
    const int symbol_top = line_bottom - 7 - (symbols->line_height - symbols->base_line);
    const int symbol_wide = draw_glyph(&symbol, symbols, SYMBOL_WAVES_UP, 0, 0, 0, 0, false);
    draw_glyph(&symbol, symbols, SYMBOL_WAVES_UP, 0, (PANEL_W - symbol_wide) / 2, symbol_top,
               solid ? ACCENT : SEARCH, true);
    add_shadow(&symbol);

    const char number[2] = {(char)('0' + status->profile % 10), '\0'};
    if (pixel_font) {
        draw_pixel_text(&number_area, number, NUMBER_X + 4, pixel_top, PIXEL_SCALE, solid ? INK : DIM, true);
    } else {
        const int number_top = line_bottom - 6 - (FG_Medium_26.line_height - FG_Medium_26.base_line);
        draw_text(&number_area, &FG_Medium_26, number, NUMBER_X + 4, number_top, 0, solid ? INK : DIM, true);
    }
    add_shadow(&number_area);
}

unsigned overlay_update(const struct fx_status *status, uint32_t ms) {
    unsigned changed = 0;

    /* a change to the output is shown, whatever was hidden */
    if (have_drawn && (status->profile != drawn.profile || status->usb != drawn.usb ||
                       status->host_connected != drawn.host_connected)) {
        revealed = true;
        revealed_at = ms;
    }
    if (!have_drawn || stale || memcmp(status, &drawn, sizeof(drawn)) != 0) {
        render(status);
        /* by value, field by field: the padding is then the same every time */
        memset(&drawn, 0, sizeof(drawn));
        drawn.battery[0] = status->battery[0];
        drawn.battery[1] = status->battery[1];
        drawn.half_connected[0] = status->half_connected[0];
        drawn.half_connected[1] = status->half_connected[1];
        drawn.charging[0] = status->charging[0];
        drawn.charging[1] = status->charging[1];
        drawn.host_connected = status->host_connected;
        drawn.usb = status->usb;
        drawn.host_open = status->host_open;
        drawn.profile_connected = status->profile_connected;
        drawn.profile = status->profile;
        have_drawn = true;
        stale = false;
        changed = (1u << OVERLAY_AREAS) - 1;
    }

    /* what fades: all there for SHOWN_MS after the change, then going */
    int fading = 0;
    if (revealed) {
        const uint32_t since = ms - revealed_at;
        fading = since < SHOWN_MS ? FULL : since < SHOWN_MS + FADE_MS ? FULL - (int)((since - SHOWN_MS) * FULL / FADE_MS) : 0;
        revealed = fading > 0;
    }
    uint8_t now[OVERLAY_AREAS];
    for (int half = 0; half < 2; half++) {
        /* a low battery flashes, a flat one fast */
        const bool off = drawn.half_connected[half] && !drawn.charging[half] &&
                         fx_battery_flash_off(drawn.battery[half], ms);
        now[half ? OVERLAY_RIGHT : OVERLAY_LEFT] = off ? 0 : FULL;
    }
    if (drawn.usb) {
        /* typing over USB: nothing to see but a change of profile */
        now[OVERLAY_SYMBOL] = now[OVERLAY_NUMBER] = (uint8_t)fading;
    } else if (drawn.host_connected) {
        now[OVERLAY_SYMBOL] = FULL;
        now[OVERLAY_NUMBER] = (uint8_t)fading;
    } else {
        now[OVERLAY_SYMBOL] = (uint8_t)pulse_at(ms);
        now[OVERLAY_NUMBER] = FULL;
    }
    for (int area = 0; area < OVERLAY_AREAS; area++) {
        if (now[area] != levels[area]) {
            levels[area] = now[area];
            changed |= 1u << area;
        }
    }
    return changed;
}

static inline uint16_t swap16(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }

static void blend(uint16_t *pixels, const uint8_t *mask, int count, int level) {
    const uint16_t halo = colors[HALO];
    const int hr = halo >> 11, hg = (halo >> 5) & 0x3f, hb = halo & 0x1f;
    for (int i = 0; i < count; i++) {
        const uint8_t m = mask[i];
        if (m == 0) {
            continue;
        }
        int alpha = m & 0x0f;
        const int color = m >> 4;
        const uint16_t under = swap16(pixels[i]);
        int r = under >> 11, g = (under >> 5) & 0x3f, b = under & 0x1f;
        /* the halo takes the picture four fifths of the way to its colour
         * at most, and all the way there under the text itself */
        const int keep = 16 - (color ? 13 : alpha * 13 / 15) * level / FULL;
        r = (r * keep + hr * (16 - keep)) / 16;
        g = (g * keep + hg * (16 - keep)) / 16;
        b = (b * keep + hb * (16 - keep)) / 16;
        if (color) {
            const uint16_t c = colors[color];
            alpha = alpha * level / FULL;
            r += ((c >> 11) - r) * alpha / 15;
            g += (((c >> 5) & 0x3f) - g) * alpha / 15;
            b += ((c & 0x1f) - b) * alpha / 15;
        }
        pixels[i] = swap16((uint16_t)(r << 11 | g << 5 | b));
    }
}

void overlay_blend_row(uint16_t *pixels, int x, int y, int count) {
    y -= OVERLAY_TOP + drop;
    if ((unsigned)y >= OVERLAY_ROWS) {
        return;
    }
    for (int a = 0; a < OVERLAY_AREAS; a++) {
        const struct area area = area_of(a);
        const int from = x > area.x ? x : area.x;
        const int end_a = area.x + area.width, end_p = x + count;
        const int to = end_p < end_a ? end_p : end_a;
        if (from < to && levels[a] > 0) {
            blend(&pixels[from - x], &area.mask[y * area.width + from - area.x], to - from, levels[a]);
        }
    }
}
