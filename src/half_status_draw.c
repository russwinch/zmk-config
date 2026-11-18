/*
 * The left half's nice!view behind the dongle, drawn upright (68x160):
 *
 *   ┌──────────┐
 *   │    85    │  this half's battery, big
 *   │ L [████ ]│  and as a bar; just a big bolt on USB power
 *   │    60    │  other half's battery, from the dongle (-- = unknown)
 *   │ R [███  ]│
 *   ├──────────┤
 *   │  ᛒ (2)   │  dongle's output: Bluetooth and its active profile
 *   └──────────┘  (filled = connected, dashed = paired, plain = free),
 *                 or USB; ↻ waiting for the dongle; ✕ no link to it
 *
 * The panel is one bit per pixel: anything drawn half grey or darker shows
 * as foreground, so the anti-aliased fonts come out a little bold.
 */

#include <stdio.h>

#include "half_status_draw.h"

#define W HALF_STATUS_DRAW_W
#define H HALF_STATUS_DRAW_H

#define BATTERY_H 58     // each battery block
#define DIVIDER_Y 119    // line above the output
#define NUMBER_FONT &lv_font_montserrat_36
#define BAR_Y 40         // bar row, within a battery block
#define OUTPUT_FONT &lv_font_montserrat_28

static lv_color_t fg, bg;

static void rect(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer, &dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

// Text in a box x..x+w-1 whose top line starts at y.
static void text(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w, const lv_font_t *font,
                 lv_text_align_t align, lv_color_t color, const char *s) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font;
    dsc.align = align;
    dsc.text = s;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t coords = {x, y, x + w - 1, y + lv_font_get_line_height(font)};
    lv_draw_label(&layer, &dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

static int32_t text_width(const lv_font_t *font, const char *s) {
    lv_point_t size;
    lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static void arc(lv_obj_t *canvas, int32_t x, int32_t y, int32_t r, int32_t start, int32_t end,
                int32_t width) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = fg;
    dsc.width = width;
    dsc.center.x = x;
    dsc.center.y = y;
    dsc.radius = r;
    dsc.start_angle = start;
    dsc.end_angle = end;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_arc(&layer, &dsc);
    lv_canvas_finish_layer(canvas, &layer);
}

// One battery: the number big, then "L [bar]" under it.
static void battery(lv_obj_t *canvas, int32_t y, const char *label, uint8_t level,
                    bool charging) {
    const bool known = level != HALF_STATUS_DRAW_UNKNOWN;
    if (known && level > 100) {
        level = 100;
    }

    const int32_t bar_y = y + BAR_Y;
    text(canvas, 0, bar_y - 2, 14, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT, fg, label);

    if (charging) {
        // On USB power the reading is the charger's voltage, not the battery's
        // (near 100 whatever the charge), so it isn't shown: a big bolt instead,
        // down across the bar's row.
        text(canvas, 14, y + 10, W - 14, NUMBER_FONT, LV_TEXT_ALIGN_CENTER, fg, LV_SYMBOL_CHARGE);
        return;
    }

    char number[4] = "--";
    if (known) {
        snprintf(number, sizeof(number), "%u", (unsigned int)level);
    }
    text(canvas, 0, y, W, NUMBER_FONT, LV_TEXT_ALIGN_CENTER, fg, number);

    // outline 50x14 at x=14, nub on the right, fill up to 46px
    const int32_t bx = 14, bw = 50, bh = 14;
    rect(canvas, bx, bar_y, bw, bh, fg);
    rect(canvas, bx + 1, bar_y + 1, bw - 2, bh - 2, bg);
    rect(canvas, bx + bw, bar_y + 4, 3, bh - 8, fg);
    if (known) {
        rect(canvas, bx + 2, bar_y + 2, (level * (bw - 4) + 50) / 100, bh - 4, fg);
    }
}

// The active profile as its number in a ring, r 12.
static void profile_badge(lv_obj_t *canvas, int32_t x, int32_t y,
                          const struct half_status_view *view) {
    const int32_t r = 12;
    if (view->profile_connected) {
        arc(canvas, x, y, r, 0, 360, r);
    } else if (view->profile_bonded) {
        const int segments = 8, gap = 20;
        for (int j = 0; j < segments; j++) {
            arc(canvas, x, y, r, 360 * j / segments + gap / 2, 360 * (j + 1) / segments - gap / 2,
                2);
        }
    }

    char label[4];
    snprintf(label, sizeof(label), "%u", (unsigned int)view->profile + 1);
    const lv_font_t *font = &lv_font_montserrat_18;
    text(canvas, x - r, y - lv_font_get_line_height(font) / 2, 2 * r, font, LV_TEXT_ALIGN_CENTER,
         view->profile_connected ? bg : fg, label);
}

static void output(lv_obj_t *canvas, const struct half_status_view *view) {
    const int32_t top = DIVIDER_Y + 2, mid = (top + H) / 2;
    const int32_t symbol_y = mid - lv_font_get_line_height(OUTPUT_FONT) / 2;
    const int32_t gap = 6;

    const char *symbol;
    bool badge = false;
    if (!view->linked) {
        symbol = LV_SYMBOL_CLOSE; // no link to the dongle
    } else if (!view->valid) {
        symbol = LV_SYMBOL_REFRESH; // linked, waiting for the dongle's first status
    } else if (view->usb) {
        symbol = LV_SYMBOL_USB;
    } else {
        symbol = LV_SYMBOL_BLUETOOTH;
        badge = true;
    }

    const int32_t symbol_w = text_width(OUTPUT_FONT, symbol);
    const int32_t x = (W - symbol_w - (badge ? gap + 24 : 0)) / 2;
    text(canvas, x, symbol_y, symbol_w + 1, OUTPUT_FONT, LV_TEXT_ALIGN_LEFT, fg, symbol);
    if (badge) {
        profile_badge(canvas, x + symbol_w + gap + 12, mid, view);
    }
}

void half_status_draw(lv_obj_t *canvas, const struct half_status_view *view, lv_color_t fg_color,
                      lv_color_t bg_color) {
    fg = fg_color;
    bg = bg_color;
    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);

    battery(canvas, 0, "L", view->own_battery, view->charging);
    battery(canvas, BATTERY_H, "R", view->linked && view->valid ? view->other_battery
                                                                : HALF_STATUS_DRAW_UNKNOWN,
            false);

    rect(canvas, 0, DIVIDER_Y, W, 2, fg);
    output(canvas, view);
}
