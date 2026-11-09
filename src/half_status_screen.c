/*
 * Waterfowl left half (peripheral of the Prospector dongle): nice!view screen
 * showing both halves' batteries and the dongle's Bluetooth profile.
 *
 * Replaces ZMK's stock peripheral screen (picture + own battery). The data the
 * half can't know itself comes from the dongle through the `half_status`
 * behavior (src/half_status.h, sent by src/dongle_half_status.c).
 *
 * Drawing follows ZMK's nice!view widgets (app/boards/shields/nice_view,
 * MIT): each 68x68 canvas is drawn upright, then rotated onto the display.
 *
 *   ┌───────────┐  top canvas
 *   │ L [███] 85│  this half's battery (bolt when charging)
 *   │ R [██ ] 60│  other half's battery, from the dongle (-- = unknown)
 *   │ ᛒ/USB   ✓ │  dongle output; link to the dongle (✕ = not connected)
 *   ├───────────┤  middle canvas
 *   │ (1)   (2) │  dongle's Bluetooth profiles: filled = active,
 *   │    (3)    │  ring = connected, dashed ring = paired
 *   │ (4)   (5) │
 *   └───────────┘  (bottom 24px left blank)
 */

#define DT_DRV_COMPAT zmk_behavior_half_status

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "half_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---------------------------------------------------------------------------
 * State from the dongle
 */

struct remote_state {
    bool valid; // false until the dongle has sent something, and after a disconnect
    uint8_t other_battery;
    uint8_t profile;
    bool usb;
    uint8_t connected; // bit per profile
    uint8_t bonded;    // bit per profile
};

static struct remote_state remote;
static K_MUTEX_DEFINE(remote_mutex);

static void redraw_work_cb(struct k_work *work);
static K_WORK_DEFINE(redraw_work, redraw_work_cb);

static void request_redraw(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &redraw_work);
    }
}

static void set_remote(const struct remote_state *next) {
    k_mutex_lock(&remote_mutex, K_FOREVER);
    bool changed = memcmp(&remote, next, sizeof(remote)) != 0;
    remote = *next;
    k_mutex_unlock(&remote_mutex);

    if (changed) {
        request_redraw();
    }
}

static struct remote_state get_remote(void) {
    k_mutex_lock(&remote_mutex, K_FOREVER);
    struct remote_state copy = remote;
    k_mutex_unlock(&remote_mutex);
    return copy;
}

/* ---------------------------------------------------------------------------
 * The `half_status` behavior: the dongle "presses" it with the status packed
 * into its two parameters. Runs in the Bluetooth receive path, so it only
 * stores the values and leaves drawing to the display thread.
 */

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int half_status_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    if (FIELD_GET(HALF_STATUS_VERSION_MASK, binding->param2) != HALF_STATUS_VERSION) {
        LOG_WRN("half status: unknown format from the dongle, ignored");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    struct remote_state next;
    memset(&next, 0, sizeof(next)); // padding too, for the memcmp in set_remote()
    next.valid = true;
    next.other_battery = FIELD_GET(HALF_STATUS_OTHER_BATTERY_MASK, binding->param1);
    next.profile = FIELD_GET(HALF_STATUS_PROFILE_MASK, binding->param1);
    next.usb = (binding->param1 & HALF_STATUS_OUTPUT_USB) != 0;
    next.connected = FIELD_GET(HALF_STATUS_CONNECTED_MASK, binding->param2);
    next.bonded = FIELD_GET(HALF_STATUS_BONDED_MASK, binding->param2);
    set_remote(&next);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int half_status_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api half_status_api = {
    .binding_pressed = half_status_pressed,
    .binding_released = half_status_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define HALF_STATUS_INST(n)                                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &half_status_api);

DT_INST_FOREACH_STATUS_OKAY(HALF_STATUS_INST)

#else
#warning "No zmk,behavior-half-status node: the screen won't get data from the dongle"
#endif

/* ---------------------------------------------------------------------------
 * Local events: own battery, USB power, link to the dongle
 */

static int half_status_local_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *link =
        as_zmk_split_peripheral_status_changed(eh);
    if (link != NULL && !link->connected) {
        // what we had from the dongle is stale until it sends again
        struct remote_state cleared;
        memset(&cleared, 0, sizeof(cleared));
        set_remote(&cleared);
    }

    request_redraw();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_status_local, half_status_local_listener);
ZMK_SUBSCRIPTION(half_status_local, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(half_status_local, zmk_split_peripheral_status_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(half_status_local, zmk_usb_conn_state_changed);
#endif

/* ---------------------------------------------------------------------------
 * Drawing (display thread only)
 */

#define CANVAS_SIZE 68
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8 // smallest format lv_draw_sw_rotate supports
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),     \
                       LV_DRAW_BUF_STRIDE_ALIGN)

#define FG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black())
#define BG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_black() : lv_color_white())

static uint8_t top_buf[CANVAS_BUF_SIZE];
static uint8_t middle_buf[CANVAS_BUF_SIZE];
static lv_obj_t *top_canvas;
static lv_obj_t *middle_canvas;

static void rotate_canvas(lv_obj_t *canvas) {
    static uint8_t copy[CANVAS_BUF_SIZE];
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    memcpy(copy, buf, sizeof(copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

static void draw_rect(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w, int32_t h,
                      lv_color_t color) {
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

static void draw_text(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w, const lv_font_t *font,
                      lv_text_align_t align, lv_color_t color, const char *text) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font;
    dsc.align = align;
    dsc.text = text;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t coords = {x, y, x + w - 1, y + CANVAS_SIZE};
    lv_draw_label(&layer, &dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

static void draw_arc(lv_obj_t *canvas, int32_t x, int32_t y, int32_t r, int32_t start,
                     int32_t end, int32_t width) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = FG;
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

// One battery row, 18px tall: "L [####] 85". level 0xFF = unknown.
static void draw_battery_row(lv_obj_t *canvas, int32_t y, const char *label, uint8_t level,
                             bool charging) {
    draw_text(canvas, 0, y, 10, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT, FG, label);

    // outline 29x12 at x=11, nub on the right, fill 0..25px
    draw_rect(canvas, 11, y + 2, 29, 12, FG);
    draw_rect(canvas, 12, y + 3, 27, 10, BG);
    if (level != HALF_STATUS_OTHER_BATTERY_UNKNOWN) {
        draw_rect(canvas, 13, y + 4, (MIN(level, 100) + 2) / 4, 8, FG);
    }
    draw_rect(canvas, 40, y + 5, 3, 6, FG);
    draw_rect(canvas, 41, y + 6, 1, 4, BG);

    if (charging) {
        draw_text(canvas, 44, y, 24, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT, FG,
                  LV_SYMBOL_CHARGE);
        return;
    }

    char text[4] = "--";
    if (level != HALF_STATUS_OTHER_BATTERY_UNKNOWN) {
        snprintf(text, sizeof(text), "%u", (unsigned int)MIN(level, 100));
    }
    draw_text(canvas, 44, y + 4, 24, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT, FG, text);
}

static void draw_top(const struct remote_state *r) {
    lv_canvas_fill_bg(top_canvas, BG, LV_OPA_COVER);

    bool charging = false;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    charging = zmk_usb_is_powered();
#endif
    draw_battery_row(top_canvas, 0, "L", zmk_battery_state_of_charge(), charging);
    draw_battery_row(top_canvas, 20, "R",
                     r->valid ? r->other_battery : HALF_STATUS_OTHER_BATTERY_UNKNOWN, false);

    draw_rect(top_canvas, 0, 42, CANVAS_SIZE, 1, FG);

    if (r->valid) {
        draw_text(top_canvas, 0, 47, 34, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT, FG,
                  r->usb ? LV_SYMBOL_USB : LV_SYMBOL_BLUETOOTH);
    }
    draw_text(top_canvas, 34, 47, 34, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT, FG,
              zmk_split_bt_peripheral_is_connected() ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(top_canvas);
}

// Same look as ZMK's central nice!view screen.
static void draw_profiles(const struct remote_state *r) {
    static const int32_t centres[HALF_STATUS_PROFILE_COUNT][2] = {
        {13, 13}, {55, 13}, {34, 34}, {13, 55}, {55, 55},
    };

    lv_canvas_fill_bg(middle_canvas, BG, LV_OPA_COVER);

    for (int i = 0; i < HALF_STATUS_PROFILE_COUNT; i++) {
        const int32_t x = centres[i][0], y = centres[i][1];
        bool selected = r->valid && i == r->profile;

        if (r->valid && (r->connected & BIT(i))) {
            draw_arc(middle_canvas, x, y, 13, 0, 360, 2);
        } else if (r->valid && (r->bonded & BIT(i))) {
            const int segments = 8, gap = 20;
            for (int j = 0; j < segments; j++) {
                draw_arc(middle_canvas, x, y, 13, 360 * j / segments + gap / 2,
                         360 * (j + 1) / segments - gap / 2, 2);
            }
        }

        if (selected) {
            draw_arc(middle_canvas, x, y, 9, 0, 359, 9);
        }

        char label[2] = {'1' + i, '\0'};
        draw_text(middle_canvas, x - 8, y - 10, 16, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER,
                  selected ? BG : FG, label);
    }

    rotate_canvas(middle_canvas);
}

static void redraw(void) {
    if (top_canvas == NULL) {
        return; // screen not built yet
    }
    struct remote_state r = get_remote();
    draw_top(&r);
    draw_profiles(&r);
}

static void redraw_work_cb(struct k_work *work) { redraw(); }

// Called by ZMK's display code (on the display thread) to build the screen.
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    // the bottom 24px aren't drawn on, so give the screen our background
    lv_obj_set_style_bg_color(screen, BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *widget = lv_obj_create(screen);
    lv_obj_remove_style_all(widget); // no theme border or padding
    lv_obj_set_size(widget, 160, 68);
    lv_obj_align(widget, LV_ALIGN_TOP_LEFT, 0, 0);

    top_canvas = lv_canvas_create(widget);
    lv_obj_align(top_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top_canvas, top_buf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    middle_canvas = lv_canvas_create(widget);
    lv_obj_align(middle_canvas, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(middle_canvas, middle_buf, CANVAS_SIZE, CANVAS_SIZE,
                         CANVAS_COLOR_FORMAT);

    redraw();
    return screen;
}
