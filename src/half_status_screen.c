/*
 * Left half behind the Prospector dongle (Waterfowl, Urchin, Aurora Corne):
 * nice!view screen
 * showing both halves' batteries and the dongle's Bluetooth profile.
 *
 * Replaces ZMK's stock peripheral screen (picture + own battery). The data the
 * half can't know itself comes from the dongle through the `half_status`
 * behavior (src/half_status.h, sent by src/dongle_half_status.c).
 *
 * The picture itself is drawn by src/half_status_draw.c (layout there;
 * tools/half_status_preview.py renders it on a computer).
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
#include "half_status_draw.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(HALF_STATUS_OTHER_BATTERY_UNKNOWN == HALF_STATUS_DRAW_UNKNOWN);

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
 * Drawing (display thread only). The picture is drawn upright on an offscreen
 * canvas (src/half_status_draw.c), then rotated onto the one on the panel,
 * as ZMK's nice!view widgets do. Only when something shown has changed: a
 * redraw costs a rotate and a panel write, so repeats from the dongle, or
 * events that change nothing on screen, leave the panel alone.
 */

#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8 // smallest format lv_draw_sw_rotate supports
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(HALF_STATUS_DRAW_W, HALF_STATUS_DRAW_H,                                     \
                       LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT), LV_DRAW_BUF_STRIDE_ALIGN)

#define FG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black())
#define BG (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_black() : lv_color_white())

static uint8_t upright_buf[CANVAS_BUF_SIZE];
static uint8_t panel_buf[CANVAS_BUF_SIZE];
static lv_obj_t *upright_canvas; // hidden, drawn on
static lv_obj_t *panel_canvas;   // shown, rotated copy
static struct half_status_view shown;
static bool drawn;

static struct half_status_view current_view(void) {
    struct remote_state r = get_remote();
    struct half_status_view v;
    memset(&v, 0, sizeof(v)); // padding too, for the memcmp in redraw()

    v.own_battery = zmk_battery_state_of_charge();
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    v.charging = zmk_usb_is_powered();
#endif
    v.linked = zmk_split_bt_peripheral_is_connected();
    v.valid = r.valid;
    if (r.valid) {
        v.other_battery = r.other_battery;
        v.usb = r.usb;
        v.profile = r.profile;
        v.profile_connected = (r.connected & BIT(r.profile)) != 0;
        v.profile_bonded = (r.bonded & BIT(r.profile)) != 0;
    } else {
        v.other_battery = HALF_STATUS_DRAW_UNKNOWN;
    }
    return v;
}

static void redraw(void) {
    if (upright_canvas == NULL) {
        return; // screen not built yet
    }
    struct half_status_view v = current_view();
    if (drawn && memcmp(&v, &shown, sizeof(v)) == 0) {
        return; // nothing on screen would change
    }
    shown = v;
    drawn = true;

    half_status_draw(upright_canvas, &v, FG, BG);
    const uint32_t src_stride =
        lv_draw_buf_width_to_stride(HALF_STATUS_DRAW_W, CANVAS_COLOR_FORMAT);
    const uint32_t dst_stride =
        lv_draw_buf_width_to_stride(HALF_STATUS_DRAW_H, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(lv_canvas_get_draw_buf(upright_canvas)->data,
                      lv_canvas_get_draw_buf(panel_canvas)->data, HALF_STATUS_DRAW_W,
                      HALF_STATUS_DRAW_H, src_stride, dst_stride, LV_DISPLAY_ROTATION_270,
                      CANVAS_COLOR_FORMAT);
    lv_obj_invalidate(panel_canvas);
}

static void redraw_work_cb(struct k_work *work) { redraw(); }

// Called by ZMK's display code (on the display thread) to build the screen.
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    upright_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(upright_canvas, upright_buf, HALF_STATUS_DRAW_W, HALF_STATUS_DRAW_H,
                         CANVAS_COLOR_FORMAT);
    lv_obj_add_flag(upright_canvas, LV_OBJ_FLAG_HIDDEN);

    panel_canvas = lv_canvas_create(screen);
    lv_obj_align(panel_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(panel_canvas, panel_buf, HALF_STATUS_DRAW_H, HALF_STATUS_DRAW_W,
                         CANVAS_COLOR_FORMAT);

    redraw();
    return screen;
}
