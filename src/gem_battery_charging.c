/*
 * Halves with a nice-view-gem screen (Urchin, Aurora Corne, both halves,
 * without the dongle): while on USB power, show only the bolt after "BAT",
 * not a percentage. The level read while charging is the charger's voltage,
 * near 100 whatever the charge, as on the dongle and the left half's screen
 * behind it.
 *
 * nice-view-gem draws "BAT 85%" (and a bolt when charging) in
 * draw_battery_status(). The link step wraps that function
 * (-Wl,--wrap in CMakeLists.txt): calls come here first, and go on to the
 * module's own unless charging. The module's drawing helpers, font and bolt
 * picture are used as they are, so the bolt sits where the module puts it.
 *
 * Only the first two members of the module's struct status_state are read
 * (battery, then charging: widgets/util.h at the commit config/west.yml
 * pins). Recheck them if that pin moves.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <lvgl.h>

struct gem_status_head {
    uint8_t battery;
    bool charging;
};

// nice-view-gem's, from widgets/util.h and assets
LV_FONT_DECLARE(pixel_operator_mono);
LV_IMG_DECLARE(bolt);
void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void canvas_draw_text(lv_obj_t *canvas, int32_t x, int32_t y, int32_t max_w,
                      lv_draw_label_dsc_t *label_dsc, const char *text);
void canvas_draw_img(lv_obj_t *canvas, int32_t x, int32_t y, const void *src,
                     lv_draw_image_dsc_t *img_dsc);

void __real_draw_battery_status(lv_obj_t *canvas, const void *state);

#define FOREGROUND (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black())

void __wrap_draw_battery_status(lv_obj_t *canvas, const void *state) {
    const struct gem_status_head *head = state;
    if (!head->charging) {
        __real_draw_battery_status(canvas, state);
        return;
    }
    // as the module's: "BAT" on the left, its bolt where it puts it when charging
    lv_draw_label_dsc_t label;
    init_label_dsc(&label, FOREGROUND, &pixel_operator_mono, LV_TEXT_ALIGN_LEFT);
    canvas_draw_text(canvas, 0, 19, 25, &label, "BAT");
    lv_draw_image_dsc_t img;
    lv_draw_image_dsc_init(&img);
    canvas_draw_img(canvas, 62, 21, &bolt, &img);
}
