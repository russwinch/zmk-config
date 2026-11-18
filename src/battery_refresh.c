/*
 * Any half: read the battery again soon after the charger is unplugged.
 *
 * ZMK reads it every ZMK_BATTERY_REPORT_INTERVAL seconds (60 by default), and
 * while charging the reading is the charger's voltage, near 100. So after
 * unplugging, the screens kept showing about 100 for up to a minute. This
 * asks ZMK for a reading BATTERY_REFRESH_DELAYS after USB power goes: one
 * once the voltage has dropped back, one once it has mostly settled.
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

extern struct k_work battery_work;

static const int delays_s[] = {5, 30};
static int next;

static void refresh_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(refresh_work, refresh_cb);

static void refresh_cb(struct k_work *work) {
    if (zmk_usb_is_powered()) {
        return; // plugged back in
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
    if (++next < (int)ARRAY_SIZE(delays_s)) {
        k_work_schedule(&refresh_work, K_SECONDS(delays_s[next] - delays_s[next - 1]));
    }
}

static int battery_refresh_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev != NULL && ev->conn_state == ZMK_USB_CONN_NONE) {
        next = 0;
        k_work_reschedule(&refresh_work, K_SECONDS(delays_s[0]));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_refresh, battery_refresh_listener);
ZMK_SUBSCRIPTION(battery_refresh, zmk_usb_conn_state_changed);
