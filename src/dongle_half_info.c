/*
 * Prospector dongle: whether each half is on USB power, beyond what ZMK hears.
 *
 * Each half sets the battery service's Battery Level Status to "charging"
 * while on USB power (src/half_charging.c). Once a half is set up, the
 * dongle reads that characteristic and subscribes to it, so the half sends it
 * again only when it changes, in a connection event it wakes for anyway:
 * nothing is polled. The screens then show a bolt beside that half's level.
 *
 * Keeping out of ZMK's way. ZMK sets each half's link up (finds the split
 * service, subscribes to the keys, the knobs and the battery) with requests
 * made from Bluetooth's receive path, which can't wait for a free ATT
 * request: with none free the subscription fails, and ZMK doesn't try again,
 * so a half whose key subscription failed connects, shows its battery and
 * types nothing. So this starts only once ZMK is done with that half (its
 * first battery reading has come, the last thing ZMK asks for, and two
 * seconds more; or 12 s after it connected, if no reading comes), and has at
 * most one request out at a time, for both halves together: the read, or the
 * subscription and the finding of its CCC.
 *
 * A request with no answer within REQUEST_TIMEOUT_S leaves that half alone
 * until it next connects (Zephyr owns the request's parameters until it
 * answers or the link goes, so they can't be used again before), and the
 * other half's goes on. Where each half got to stays on the Debug screen
 * (dongle_half_info_progress), so a half that never shows a bolt says why.
 *
 * After a half is unplugged its next battery reading is a second or so
 * away, and the level until then is the charger's, near 100: for up to
 * SETTLE_MS, or until that reading comes, dongle_half_settling() says so and
 * the screens show a spinner in place of the level.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#include "dongle_half_info.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Battery Level Status: flags (1 byte), then power state (2, little endian),
// whose bits 5-6 are the charge state (Bluetooth's GATT Specification
// Supplement; Zephyr's subsys/bluetooth/services/bas/bas_bls.c).
#define POWER_STATE_CHARGE_SHIFT 5
#define POWER_STATE_CHARGE_MASK (BIT_MASK(2) << POWER_STATE_CHARGE_SHIFT)
#define CHARGE_STATE_CHARGING 1

#define READY_AFTER_BATTERY_S 2 // after ZMK's first battery reading of the half
#define READY_AT_MOST_S 12      // after the half connected, reading or not
#define RETRY_S 5               // a failed read or subscription, again after this
#define RETRIES 5
#define REQUEST_TIMEOUT_S 10 // a request with no answer by then: that half is left alone
#define SETTLE_MS 3000       // after unplugging, a fresh battery reading within this

// Where a half has got to (the Debug screen shows it).
enum stage {
    NOT_CONNECTED,
    WAITING,     // for ZMK to finish with the half
    QUEUED,      // ready, behind the other half's request, or waiting to retry
    READING,     // the read is out
    SUBSCRIBING, // the subscription (finding the CCC, writing it) is out
    SUBSCRIBED,  // done: the half sends changes
    NOT_FOUND,   // done: the half has no Battery Level Status (older firmware)
    FAILED,      // done: RETRIES tries failed, the last error kept
    TIMED_OUT,   // done: a request had no answer
};

struct half {
    struct bt_gatt_read_params read;
    struct bt_gatt_subscribe_params sub;
    struct bt_gatt_discover_params disc; // for the subscription to find the CCC
};

static struct half halves[2];
static struct k_spinlock lock;
static struct bt_conn *conns[2]; // a reference to each half's link, under lock
static atomic_t stage[2];
static atomic_t tries[2];
static atomic_t last_err[2];   // ATT error, or 0x80 | errno; 0 for none
static atomic_t not_before[2]; // uptime ms: a retry waits until then
static atomic_t charging;      // bit per half
static atomic_t settling;      // bit per half: unplugged, no battery reading since
static atomic_t unplugged_at[2];
#define IDLE (-1)
static atomic_t inflight = ATOMIC_INIT(IDLE); // the half whose request is out, or IDLE

bool dongle_half_charging(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&charging, half);
}

bool dongle_half_settling(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&settling, half) &&
           k_uptime_get_32() - (uint32_t)atomic_get(&unplugged_at[half]) < SETTLE_MS;
}

void dongle_half_info_progress(int half, char code[3], uint8_t *err) {
    static const char codes[][3] = {
        [NOT_CONNECTED] = "", [WAITING] = "WT",    [QUEUED] = "Q",   [READING] = "R",
        [SUBSCRIBING] = "S",  [SUBSCRIBED] = "OK", [NOT_FOUND] = "NF", [FAILED] = "E", [TIMED_OUT] = "TO",
    };
    code[0] = code[1] = code[2] = '\0';
    *err = 0;
    if (half < 0 || half >= 2) {
        return;
    }
    const atomic_val_t s = atomic_get(&stage[half]);
    if (s < 0 || s >= (atomic_val_t)ARRAY_SIZE(codes)) {
        return;
    }
    memcpy(code, codes[s], 3);
    if (s == QUEUED || s == READING || s == SUBSCRIBING) {
        code[1] = (char)('1' + MIN(atomic_get(&tries[half]), 8)); // the try, 1 on
    }
    *err = (uint8_t)atomic_get(&last_err[half]);
}

static void note_err(int half, int err) {
    if (err != 0) {
        atomic_set(&last_err[half], err < 0 ? 0x80 | MIN(-err, 0x7f) : MIN(err, 0x7f));
    }
}

// Which half a connection is: the slot ZMK keeps its address in.
static int half_of(struct bt_conn *conn) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||
        info.role != BT_CONN_ROLE_CENTRAL) {
        return -1;
    }
    const int slot = zmk_ble_put_peripheral_addr(bt_conn_get_dst(conn));
    return slot >= 0 && slot < 2 ? slot : -1;
}

// Which half a read or subscription belongs to.
static int half_for(const void *params) {
    for (int h = 0; h < 2; h++) {
        if (params == &halves[h].read || params == &halves[h].sub) {
            return h;
        }
    }
    return -1;
}

/* --- the one request out at a time ------------------------------------------- */

static void step_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(step_work, step_cb);
static void timeout_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_cb);

static void step_soon(void) { k_work_reschedule(&step_work, K_NO_WAIT); }

// The request out for `half` is over: done (in `final`), or to be tried again.
static void finish(int half, enum stage final, bool retry) {
    if (!atomic_cas(&inflight, half, IDLE)) {
        return; // not the one out (its half disconnected, or it timed out)
    }
    k_work_cancel_delayable(&timeout_work);
    if (retry && atomic_inc(&tries[half]) + 1 < RETRIES) {
        atomic_set(&not_before[half], (atomic_val_t)(k_uptime_get_32() + RETRY_S * 1000));
        atomic_set(&stage[half], QUEUED);
    } else {
        atomic_set(&stage[half], retry ? FAILED : final);
    }
    step_soon();
}

// The one out has had no answer: leave its half alone until it reconnects.
static void timeout_cb(struct k_work *work) {
    const atomic_val_t half = atomic_get(&inflight);
    if (half == IDLE || !atomic_cas(&inflight, half, IDLE)) {
        return;
    }
    atomic_set(&stage[half], TIMED_OUT);
    LOG_WRN("half %d: no answer about charging", (int)half);
    step_soon();
}

// A value, read or notified; NULL when there is none.
static void take(int half, const void *data, uint16_t length) {
    bool on = false;
    if (data != NULL && length >= 3) {
        const uint16_t power = sys_get_le16((const uint8_t *)data + 1);
        on = FIELD_GET(POWER_STATE_CHARGE_MASK, power) == CHARGE_STATE_CHARGING;
    }
    const bool was = atomic_test_bit(&charging, half);
    atomic_set_bit_to(&charging, half, on);
    if (was && !on) {
        atomic_set(&unplugged_at[half], (atomic_val_t)k_uptime_get_32());
        atomic_set_bit(&settling, half);
    } else if (on) {
        atomic_clear_bit(&settling, half);
    }
}

static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length) {
    const int half = half_for(params);
    if (half < 0) {
        return BT_GATT_ITER_STOP;
    }
    if (data == NULL) {
        // not subscribed (no CCC found, or writing it failed), or no longer:
        // the link has gone (the subscription is volatile)
        params->value_handle = 0;
        if (atomic_get(&inflight) == half) {
            note_err(half, -ENOENT);
            finish(half, FAILED, true);
        }
        return BT_GATT_ITER_STOP;
    }
    take(half, data, length);
    return BT_GATT_ITER_CONTINUE;
}

// The CCC written (or not).
static void subscribed_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params) {
    const int half = half_for(params);
    if (half < 0) {
        return;
    }
    note_err(half, err);
    finish(half, SUBSCRIBED, err != 0);
}

static void subscribe(struct bt_conn *conn, int half, uint16_t value_handle) {
    struct half *h = &halves[half];
    memset(&h->sub, 0, sizeof(h->sub));
    h->sub.notify = notify_cb;
    h->sub.subscribe = subscribed_cb;
    h->sub.value = BT_GATT_CCC_NOTIFY;
    h->sub.value_handle = value_handle;
    h->sub.ccc_handle = 0; // found by Zephyr, from the value on
    h->sub.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    h->sub.disc_params = &h->disc;
    atomic_set_bit(h->sub.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE); // gone with the link
    atomic_set(&stage[half], SUBSCRIBING);
    k_work_reschedule(&timeout_work, K_SECONDS(REQUEST_TIMEOUT_S)); // a new request: its own time
    const int err = bt_gatt_subscribe(conn, &h->sub);
    if (err == -EALREADY) {
        finish(half, SUBSCRIBED, false);
    } else if (err) {
        note_err(half, err);
        finish(half, FAILED, true);
    }
    // else subscribed_cb, or notify_cb with no data, says how it went
}

// Called once: with the value (and its handle in by_uuid), or with no data
// after an error. Zephyr only calls again after a value if asked to go on.
static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                       const void *data, uint16_t length) {
    const int half = half_for(params);
    if (half < 0 || atomic_get(&inflight) != half) {
        return BT_GATT_ITER_STOP;
    }
    if (err == BT_ATT_ERR_ATTRIBUTE_NOT_FOUND || (!err && data == NULL)) {
        finish(half, NOT_FOUND, false); // older firmware
        return BT_GATT_ITER_STOP;
    }
    if (err) {
        note_err(half, err);
        finish(half, FAILED, true);
        return BT_GATT_ITER_STOP;
    }
    take(half, data, length);
    subscribe(conn, half, params->by_uuid.start_handle);
    return BT_GATT_ITER_STOP;
}

// On the system work queue: send the next request, if none is out.
static void step_cb(struct k_work *work) {
    if (atomic_get(&inflight) != IDLE) {
        return; // its end steps again
    }
    int32_t wait_ms = 0; // until a half waiting for a retry may go
    for (int half = 0; half < 2; half++) {
        if (atomic_get(&stage[half]) != QUEUED) {
            continue;
        }
        const int32_t wait = (int32_t)((uint32_t)atomic_get(&not_before[half]) - k_uptime_get_32());
        if (wait > 0) {
            wait_ms = wait_ms == 0 ? wait : MIN(wait_ms, wait);
            continue;
        }
        struct bt_conn *conn = NULL;
        K_SPINLOCK(&lock) {
            conn = conns[half] != NULL ? bt_conn_ref(conns[half]) : NULL;
        }
        if (conn == NULL) {
            continue;
        }
        struct bt_gatt_read_params *p = &halves[half].read;
        memset(p, 0, sizeof(*p));
        p->func = read_cb;
        p->handle_count = 0; // read by UUID
        p->by_uuid.uuid = BT_UUID_BAS_BATTERY_LEVEL_STATUS;
        p->by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
        p->by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
        atomic_set(&inflight, half);
        atomic_set(&stage[half], READING);
        k_work_reschedule(&timeout_work, K_SECONDS(REQUEST_TIMEOUT_S));
        const int err = bt_gatt_read(conn, p);
        bt_conn_unref(conn);
        if (err) {
            note_err(half, err);
            finish(half, FAILED, true);
        }
        return; // one at a time
    }
    if (wait_ms > 0) {
        k_work_schedule(&step_work, K_MSEC(wait_ms));
    }
}

/* --- when to start: once ZMK is done with the half ----------------------------- */

static struct k_work_delayable ready_work[2];

static void ready_cb(struct k_work *work) {
    const int half = k_work_delayable_from_work(work) == &ready_work[1] ? 1 : 0;
    if (atomic_cas(&stage[half], WAITING, QUEUED)) {
        atomic_clear(&tries[half]);
        atomic_clear(&not_before[half]);
        step_soon();
    }
}

static int half_info_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL || ev->source >= 2) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    // a fresh reading: no longer the charger's
    atomic_clear_bit(&settling, ev->source);
    // ZMK's first reading of a half: it is done with it. ZMK's source and
    // our half both come from ZMK's peripheral slots, so they agree; if they
    // ever didn't, READY_AT_MOST_S still starts the other.
    if (atomic_get(&stage[ev->source]) == WAITING) {
        k_work_reschedule(&ready_work[ev->source], K_SECONDS(READY_AFTER_BATTERY_S));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_half_info, half_info_listener);
ZMK_SUBSCRIPTION(dongle_half_info, zmk_peripheral_battery_state_changed);

static void connected(struct bt_conn *conn, uint8_t err) {
    const int half = err ? -1 : half_of(conn);
    if (half < 0) {
        return;
    }
    K_SPINLOCK(&lock) {
        if (conns[half] != NULL) {
            bt_conn_unref(conns[half]);
        }
        conns[half] = bt_conn_ref(conn);
    }
    atomic_clear(&last_err[half]);
    atomic_set(&stage[half], WAITING);
    k_work_reschedule(&ready_work[half], K_SECONDS(READY_AT_MOST_S));
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    int half = -1;
    K_SPINLOCK(&lock) {
        for (int h = 0; h < 2; h++) {
            if (conns[h] == conn) {
                bt_conn_unref(conns[h]);
                conns[h] = NULL;
                half = h;
            }
        }
    }
    if (half < 0) {
        return;
    }
    k_work_cancel_delayable(&ready_work[half]);
    atomic_set(&stage[half], NOT_CONNECTED);
    atomic_clear_bit(&charging, half);
    atomic_clear_bit(&settling, half);
    // a request out to it won't be answered: let the other half's go
    if (atomic_cas(&inflight, half, IDLE)) {
        k_work_cancel_delayable(&timeout_work);
    }
    step_soon();
}

BT_CONN_CB_DEFINE(dongle_half_info_cb) = {
    .connected = connected,
    .disconnected = disconnected,
};

static int dongle_half_info_init(void) {
    k_work_init_delayable(&ready_work[0], ready_cb);
    k_work_init_delayable(&ready_work[1], ready_cb);
    return 0;
}

SYS_INIT(dongle_half_info_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
