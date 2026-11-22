/*
 * Every left half with a nice!view (Waterfowl, Aurora Corne): a screen
 * showing both halves' batteries and the keyboard's output (USB, or the
 * active Bluetooth profile), in place of ZMK's stock picture.
 *
 * On its own the left half is the central and knows all of it: its own
 * battery, the right half's (which it fetches), whether the right half is on
 * USB power (read as the dongle does, src/dongle_half_info.c) and its own
 * output. Behind
 * the Prospector dongle it is a peripheral, and what it can't know itself
 * comes from the dongle through the `half_status` behavior
 * (src/half_status.h, sent by src/dongle_half_status.c).
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
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/split/central.h>
#else
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#endif

#include "battery_refresh.h"
#include "half_status.h"
#include "half_status_draw.h"
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
#include "dongle_half_info.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(HALF_STATUS_OTHER_BATTERY_UNKNOWN == HALF_STATUS_DRAW_UNKNOWN);

/* ---------------------------------------------------------------------------
 * State from the dongle
 */

struct remote_state {
    bool valid; // false until the dongle has sent something, and after a disconnect
    uint8_t other_battery;
    bool other_charging;
    bool other_estimate;
    bool own_estimate; // this half's level is the dongle's estimate, own_battery
    uint8_t own_battery;
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

// e.g. the right half's charging state changed (src/dongle_half_info.c)
void half_status_screen_redraw(void) { request_redraw(); }

static void set_remote(const struct remote_state *next) {
    k_mutex_lock(&remote_mutex, K_FOREVER);
    bool changed = memcmp(&remote, next, sizeof(remote)) != 0;
    remote = *next;
    k_mutex_unlock(&remote_mutex);

    if (changed) {
        request_redraw();
    }
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) // on its own the half knows it all itself
static struct remote_state get_remote(void) {
    k_mutex_lock(&remote_mutex, K_FOREVER);
    struct remote_state copy = remote;
    k_mutex_unlock(&remote_mutex);
    return copy;
}
#endif

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
    next.other_charging = (binding->param1 & HALF_STATUS_OTHER_CHARGING) != 0;
    next.other_estimate = (binding->param1 & HALF_STATUS_OTHER_ESTIMATE) != 0;
    next.own_estimate = (binding->param1 & HALF_STATUS_OWN_ESTIMATE) != 0;
    next.own_battery = FIELD_GET(HALF_STATUS_OWN_BATTERY_MASK, binding->param1);
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
 * Local events: own battery, USB power; behind the dongle the link to it, on
 * its own the right half's battery and the output
 */

static int half_status_local_listener(const zmk_event_t *eh) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    const struct zmk_split_peripheral_status_changed *link =
        as_zmk_split_peripheral_status_changed(eh);
    if (link != NULL && !link->connected) {
        // what we had from the dongle is stale until it sends again
        struct remote_state cleared;
        memset(&cleared, 0, sizeof(cleared));
        set_remote(&cleared);
    }
#endif

    request_redraw();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_status_local, half_status_local_listener);
ZMK_SUBSCRIPTION(half_status_local, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(half_status_local, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(half_status_local, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(half_status_local, zmk_endpoint_changed);
#else
ZMK_SUBSCRIPTION(half_status_local, zmk_split_peripheral_status_changed);
#endif
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

#define BOLT_FLASH_MS 1000 // an estimated level's bolt: on this long, then off as long
static bool flash_off;
static void flash_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(flash_work, flash_work_cb);

static struct half_status_view current_view(void) {
    struct half_status_view v;
    memset(&v, 0, sizeof(v)); // padding too, for the memcmp in redraw()

    v.own_battery = zmk_battery_state_of_charge();
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#if IS_ENABLED(CONFIG_BATTERY_REFRESH_ON_UNPLUG)
    v.charging = battery_refresh_shows_usb_power(); // until the battery is read after unplugging
#else
    v.charging = zmk_usb_is_powered();
#endif
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // on its own: everything is here
    v.linked = v.valid = true;
    uint8_t level = 0;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    zmk_split_central_get_peripheral_battery_level(0, &level);
#endif
    v.other_battery = level > 0 ? level : HALF_STATUS_DRAW_UNKNOWN; // 0: not heard from yet
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
    v.other_charging = dongle_half_charging(0); // the right half: ZMK's only peripheral here
#endif
    v.usb = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    v.profile = zmk_ble_active_profile_index();
    v.profile_connected = zmk_ble_active_profile_is_connected();
    v.profile_bonded = !zmk_ble_active_profile_is_open();
    return v;
#else
    struct remote_state r = get_remote();
    v.linked = zmk_split_bt_peripheral_is_connected();
    v.valid = r.valid;
    if (r.valid) {
        v.other_battery = r.other_battery;
        v.other_charging = r.other_charging;
        v.other_estimate = r.other_estimate;
        if (r.own_estimate) { // ZMK's own reads high under the charger
            v.own_battery = r.own_battery;
            v.estimate = true;
        }
        v.usb = r.usb;
        v.profile = r.profile;
        v.profile_connected = (r.connected & BIT(r.profile)) != 0;
        v.profile_bonded = (r.bonded & BIT(r.profile)) != 0;
    } else {
        v.other_battery = HALF_STATUS_DRAW_UNKNOWN;
    }
    return v;
#endif
}

static void redraw(void) {
    if (upright_canvas == NULL) {
        return; // screen not built yet
    }
    struct half_status_view v = current_view();
    // a bolt beside an estimated level flashes until 100: drawn again for each change
    if ((v.charging && v.estimate && v.own_battery < 100) ||
        (v.other_charging && v.other_estimate && v.other_battery < 100)) {
        v.bolt_off = flash_off;
        k_work_schedule_for_queue(zmk_display_work_q(), &flash_work, K_MSEC(BOLT_FLASH_MS));
    } else {
        flash_off = false;
    }
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

static void flash_work_cb(struct k_work *work) {
    flash_off = !flash_off;
    redraw();
}

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
