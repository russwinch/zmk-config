/*
 * Any half: read the battery again soon after the charger is unplugged.
 *
 * ZMK reads it every ZMK_BATTERY_REPORT_INTERVAL seconds (60 by default), and
 * while charging the reading is the charger's voltage, near 100. So after
 * unplugging, the screens kept showing about 100 for up to a minute. This
 * asks ZMK for a reading a second after USB power goes, once the charger's
 * voltage has gone (it drops within milliseconds), then at 10 and 30 seconds
 * as the battery settles the last few percent. Each is a few milliseconds
 * of the ADC.
 *
 * Until that first reading has been taken the half still counts as on USB
 * power (battery_refresh_shows_usb_power), so its nice!view and the dongle
 * keep the bolt rather than show the charger's reading for that second.
 * Then both are told at once: the nice!view is redrawn, and the dongle hears
 * the new level and, after it, "discharging" (src/half_charging.c).
 *
 * ZMK has no call for this: it submits ZMK's own battery work item
 * (app/src/battery.c's battery_work, which isn't static) to ZMK's low
 * priority queue, as ZMK's timer does. If ZMK renames it, the link fails.
 */

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

#include "battery_refresh.h"
#include "half_status.h"

extern struct k_work battery_work;

static const int delays_ms[] = {1000, 10000, 30000};
static int next;

// The battery has been read since USB power last went. Cleared whenever the
// half is seen on USB power, so it is already false when it is unplugged.
static atomic_t read_since_unplug = ATOMIC_INIT(1);

bool battery_refresh_shows_usb_power(void) {
    if (zmk_usb_is_powered()) {
        atomic_clear(&read_since_unplug);
        return true;
    }
    return !atomic_get(&read_since_unplug);
}

// Runs on ZMK's low priority queue after the first reading (the queue runs in
// order), so the level has gone out before the charging state changes.
static void read_cb(struct k_work *work) {
    if (zmk_usb_is_powered()) {
        return; // plugged back in
    }
    atomic_set(&read_since_unplug, 1);
#if IS_ENABLED(CONFIG_HALF_CHARGING_STATUS)
    half_charging_update(); // the dongle
#endif
#if IS_ENABLED(CONFIG_WATERFOWL_HALF_STATUS_SCREEN)
    half_status_screen_redraw(); // its own nice!view
#endif
}
static K_WORK_DEFINE(read_work, read_cb);

static void refresh_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(refresh_work, refresh_cb);

static void refresh_cb(struct k_work *work) {
    if (zmk_usb_is_powered()) {
        return; // plugged back in
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
    if (next == 0) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &read_work);
    }
    if (++next < (int)ARRAY_SIZE(delays_ms)) {
        k_work_schedule(&refresh_work, K_MSEC(delays_ms[next] - delays_ms[next - 1]));
    }
}

static int battery_refresh_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev != NULL && ev->conn_state != ZMK_USB_CONN_NONE) {
        atomic_clear(&read_since_unplug);
    } else if (ev != NULL) {
        next = 0;
        k_work_reschedule(&refresh_work, K_MSEC(delays_ms[0]));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_refresh, battery_refresh_listener);
ZMK_SUBSCRIPTION(battery_refresh, zmk_usb_conn_state_changed);
