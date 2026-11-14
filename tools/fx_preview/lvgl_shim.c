/*
 * See lvgl.h here. Looks a character up in one of the module's fonts and
 * hands back its size and its raw 4-bit picture, as LVGL does for
 * status_overlay.c on the dongle. Kerning is left out: the status text is
 * digits in fonts that don't kern them.
 */

#include "lvgl.h"

static uint32_t glyph_id(const lv_font_fmt_txt_dsc_t *font, uint32_t letter) {
    for (int i = 0; i < font->cmap_num; i++) {
        const lv_font_fmt_txt_cmap_t *map = &font->cmaps[i];
        const uint32_t at = letter - map->range_start;
        if (letter < map->range_start || at >= map->range_length) {
            continue;
        }
        if (map->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY) {
            return map->glyph_id_start + at;
        }
        if (map->type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) {
            for (int n = 0; n < map->list_length; n++) {
                if (map->unicode_list[n] == at) {
                    return map->glyph_id_start + n;
                }
            }
        }
    }
    return 0;
}

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next) {
    (void)next;
    const lv_font_fmt_txt_dsc_t *data = font->dsc;
    const uint32_t id = glyph_id(data, letter);
    if (id == 0) {
        return false;
    }
    const lv_font_fmt_txt_glyph_dsc_t *glyph = &data->glyph_dsc[id];
    dsc->resolved_font = font;
    dsc->adv_w = (uint16_t)((glyph->adv_w + 8) >> 4);
    dsc->box_w = glyph->box_w;
    dsc->box_h = glyph->box_h;
    dsc->ofs_x = glyph->ofs_x;
    dsc->ofs_y = glyph->ofs_y;
    dsc->format = data->bpp == 4 ? LV_FONT_GLYPH_FORMAT_A4 : LV_FONT_GLYPH_FORMAT_NONE;
    dsc->glyph_index = id;
    return true;
}

const void *lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf) {
    (void)draw_buf;
    const lv_font_fmt_txt_dsc_t *data = dsc->resolved_font->dsc;
    return &data->glyph_bitmap[data->glyph_dsc[dsc->glyph_index].bitmap_index];
}

bool lv_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next) {
    return font->get_glyph_dsc(font, dsc, letter, next);
}

const void *lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf) {
    return dsc->resolved_font->get_glyph_bitmap(dsc, draw_buf);
}
