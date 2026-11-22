/* tools/half_status_preview.py: draws the half's screen for each case named
 * on the command line and writes it as a PGM, as the panel shows it (160x68,
 * the picture rotated), one file per case: <out>/<case>.pgm. */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "half_status_draw.h"

#define CF LV_COLOR_FORMAT_L8
static uint8_t upright[HALF_STATUS_DRAW_W * HALF_STATUS_DRAW_H];
static uint8_t panel[HALF_STATUS_DRAW_W * HALF_STATUS_DRAW_H];
static uint8_t disp_buf[160 * 68 * 2];

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *p) { lv_display_flush_ready(d); }

static struct half_status_view make(const char *name) {
    struct half_status_view v;
    memset(&v, 0, sizeof(v));
    v.own_battery = 85;
    v.other_battery = 62;
    v.linked = true;
    v.valid = true;
    v.profile = 1;
    v.profile_connected = true;
    v.profile_bonded = true;
    if (!strcmp(name, "usb")) { v.usb = true; v.own_battery = 100; v.charging = true; }
    if (!strcmp(name, "charging")) { v.own_battery = 47; v.charging = true; v.usb = true; }
    if (!strcmp(name, "right_charging")) { v.other_battery = 100; v.other_charging = true; }
    if (!strcmp(name, "estimate")) { v.own_battery = 63; v.charging = true; v.estimate = true; v.usb = true; }
    if (!strcmp(name, "estimate_off")) { v.own_battery = 63; v.charging = true; v.estimate = true; v.usb = true; v.bolt_off = true; }
    if (!strcmp(name, "right_estimate")) { v.other_battery = 71; v.other_charging = true; v.other_estimate = true; }
    if (!strcmp(name, "not_known")) { v.other_battery = 0xFF; v.other_charging = true; v.other_estimate = true; }
    if (!strcmp(name, "paired")) { v.profile = 2; v.profile_connected = false; v.own_battery = 9; }
    if (!strcmp(name, "free")) { v.profile = 4; v.profile_connected = false; v.profile_bonded = false; }
    if (!strcmp(name, "waiting")) { v.valid = false; }
    if (!strcmp(name, "no_dongle")) { v.linked = false; v.valid = false; v.own_battery = 100; }
    return v;
}

int main(int argc, char **argv) {
    lv_init();
    lv_display_t *d = lv_display_create(160, 68);
    lv_display_set_flush_cb(d, flush);
    lv_display_set_buffers(d, disp_buf, NULL, sizeof(disp_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_t *canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, upright, HALF_STATUS_DRAW_W, HALF_STATUS_DRAW_H, CF);

    for (int i = 2; i < argc; i++) {
        struct half_status_view v = make(argv[i]);
        half_status_draw(canvas, &v, lv_color_black(), lv_color_white());
        lv_draw_sw_rotate(upright, panel, HALF_STATUS_DRAW_W, HALF_STATUS_DRAW_H,
                          HALF_STATUS_DRAW_W, HALF_STATUS_DRAW_H, LV_DISPLAY_ROTATION_270, CF);
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.pgm", argv[1], argv[i]);
        FILE *f = fopen(path, "wb");
        fprintf(f, "P5\n%d %d\n255\n", HALF_STATUS_DRAW_H, HALF_STATUS_DRAW_W);
        fwrite(panel, 1, sizeof(panel), f);
        fclose(f);
    }
    return 0;
}
