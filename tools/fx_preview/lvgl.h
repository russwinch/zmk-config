/*
 * Just enough of LVGL for tools/fx_preview.py to build the dongle's status
 * text (src/screen_fx/status_overlay.c) and the Prospector module's font
 * files on a computer: the font structures those files fill in, and the two
 * calls the status text makes. See lvgl_shim.c. Not used in the firmware.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 0
#define LV_VERSION_CHECK(x, y, z) 1
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_SUBPX_NONE 0
#define LV_FONT_DECLARE(name) extern const lv_font_t name;

enum { LV_FONT_GLYPH_FORMAT_NONE, LV_FONT_GLYPH_FORMAT_A1, LV_FONT_GLYPH_FORMAT_A2, LV_FONT_GLYPH_FORMAT_A4 };

enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
};

typedef struct {
    uint32_t bitmap_index;
    uint32_t adv_w; /* in 16ths of a pixel */
    uint8_t box_w, box_h;
    int8_t ofs_x, ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint16_t list_length;
    uint8_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const int8_t *class_pair_values;
    const uint8_t *left_class_mapping, *right_class_mapping;
    uint8_t left_class_cnt, right_class_cnt;
} lv_font_fmt_txt_kern_classes_t;

typedef struct {
    const void *glyph_ids;
    const int8_t *values;
    uint32_t pair_cnt;
    uint8_t glyph_ids_size;
} lv_font_fmt_txt_kern_pair_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num, bpp, kern_classes, bitmap_format;
} lv_font_fmt_txt_dsc_t;

typedef struct {
    int unused;
} lv_draw_buf_t;

typedef struct lv_font_t lv_font_t;

typedef struct {
    const lv_font_t *resolved_font;
    uint16_t adv_w, box_w, box_h;
    int16_t ofs_x, ofs_y;
    uint8_t format;
    uint8_t req_raw_bitmap;
    uint32_t glyph_index;
} lv_font_glyph_dsc_t;

struct lv_font_t {
    bool (*get_glyph_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t letter, uint32_t next);
    const void *(*get_glyph_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);
    int32_t line_height, base_line;
    uint8_t subpx;
    int8_t underline_position, underline_thickness;
    const void *dsc;
    const lv_font_t *fallback;
    void *user_data;
};

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next);
const void *lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf);
bool lv_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next);
const void *lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf);
