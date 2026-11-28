/*
 * A half behind the dongle: closes its link itself before going to sleep or
 * restarting into its bootloader, saying which (src/half_goodbye.h), so the
 * dongle shows it asleep or updating rather than as a dropped link. Left to
 * itself the half just stops, and the dongle hears only silence.
 *
 * Sleep: ZMK raises its activity event before suspending the devices and
 * powering off, in the same work item, so this closes the link from the
 * listener. Updating: the Keyboard Writer app's restart, like the keymap's
 * bootloader key, sets the boot mode to the bootloader and calls
 * sys_reboot, which is wrapped at link time (CMakeLists.txt). Any other
 * restart goes ahead as before, with no goodbye.
 *
 * The controller sends the close in the link's next connection event; this
 * waits for the link to end, or GOODBYE_WAIT_MS. Both callers hold the
 * system work queue, so ZMK can't start advertising again before the half
 * stops.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#include "half_goodbye.h"

#define GOODBYE_WAIT_MS 300

static K_SEM_DEFINE(gone, 0, 1);
static atomic_t closing;

static void goodbye_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (atomic_get(&closing)) {
        k_sem_give(&gone);
    }
}

BT_CONN_CB_DEFINE(half_goodbye_conn) = {
    .disconnected = goodbye_disconnected,
};

static void close_link(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED &&
        bt_conn_disconnect(conn, *(uint8_t *)data) == 0) {
        atomic_inc(&closing);
    }
}

static void goodbye(uint8_t reason) {
    if (k_is_in_isr() || atomic_get(&closing)) {
        return;
    }
    k_sem_reset(&gone);
    bt_conn_foreach(BT_CONN_TYPE_LE, close_link, &reason);
    const int64_t until = k_uptime_get() + GOODBYE_WAIT_MS;
    for (int left = (int)atomic_get(&closing); left > 0; left--) {
        const int64_t wait = until - k_uptime_get();
        if (wait <= 0 || k_sem_take(&gone, K_MSEC(wait)) != 0) {
            break;
        }
    }
    atomic_set(&closing, 0);
}

static int goodbye_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev != NULL && ev->state == ZMK_ACTIVITY_SLEEP) {
        goodbye(HALF_GOODBYE_SLEEP);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_goodbye, goodbye_listener);
ZMK_SUBSCRIPTION(half_goodbye, zmk_activity_state_changed);

#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
FUNC_NORETURN void __real_sys_reboot(int type);

FUNC_NORETURN void __wrap_sys_reboot(int type) {
    if (bootmode_check(BOOT_MODE_TYPE_BOOTLOADER) > 0) {
        goodbye(HALF_GOODBYE_UPDATE);
    }
    __real_sys_reboot(type);
}
#endif
