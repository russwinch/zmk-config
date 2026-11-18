/* LVGL settings for tools/half_status_preview.py: just enough to draw the
 * half's screen (src/half_status_draw.c) offscreen. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (256 * 1024)
#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_CANVAS 1
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#endif
