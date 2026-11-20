/*
 * Prospector dongle: what it hears from each half beyond what ZMK does.
 *
 *   on USB power   the battery service's Battery Level Status (set by
 *                  src/half_charging.c): the screens show a bolt for that
 *                  half instead of its level
 *   temperature    the Environmental Sensing Service's Temperature (offered
 *                  by src/half_temperature.c): the Debug screen shows it
 *   transmit power the Tx Power Service's Tx Power Level (offered by
 *                  src/half_tx_power.c): the Debug screen shows it
 *   battery volts  the Voltage characteristic (offered by
 *                  src/half_voltage.c): the Debug screen shows it
 *
 * Each is read once a half is set up, and then subscribed to: the half sends
 * it again only when it changes, in a connection event it wakes for anyway.
 * The transmit power is fixed when the half's firmware is built, so it is
 * only read. Nothing is polled, so it costs the halves' batteries next to
 * nothing. A half with firmware from before this has none of them: it reads
 * as not charging, temperature and transmit power unknown.
 *
 * Keeping out of ZMK's way. ZMK sets each half's link up (finds the split
 * service, subscribes to the keys, the knobs and the battery) with requests
 * made from Bluetooth's receive path, which can't wait for a free ATT
 * request: with none free the subscription fails, and ZMK doesn't try again,
 * so a half whose key subscription failed connects, shows its battery and
 * types nothing. The ATT requests are a pool shared by every link
 * (CONFIG_BT_ATT_TX_COUNT). So this starts only once ZMK is done with that
 * half (its first battery reading has come, the last thing ZMK asks for,
 * and two seconds more; or 12 s after it connected, if no reading comes),
 * and has at most one request out at a time, for both halves together: a
 * read, or a subscription and the finding of its CCC.
 *
 * A request with no answer within REQUEST_TIMEOUT_S leaves that half alone
 * until it next connects (its request may still be out: Zephyr owns its
 * parameters until it answers or the link goes), so the other half's reads
 * still go. The Debug screen shows how far each half has got
 * (dongle_half_info_progress).
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
#define SETTLE_MS 3000 // after unplugging, a fresh battery reading within this
#define REQUEST_TIMEOUT_S 10 // a request with no answer by then: that half is left alone

enum { BLS, TEMP, TX_POWER, VOLTS, KINDS };

// One characteristic on one half: read once, then subscribed to.
struct watch {
    struct bt_gatt_read_params read;
    struct bt_gatt_subscribe_params sub;
    struct bt_gatt_discover_params disc; // for the subscription to find the CCC
};

static struct watch watches[2][KINDS];
static atomic_t charging;   // bit per half
static atomic_t temp_known; // bit per half
static atomic_t temp_c[2];
static atomic_t tx_known; // bit per half
static atomic_t tx_dbm[2];
static atomic_t millivolts[2]; // 0: not known
static atomic_t settling; // bit per half: unplugged, no battery reading since
static atomic_t unplugged_at[2];

bool dongle_half_charging(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&charging, half);
}

bool dongle_half_settling(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&settling, half) &&
           k_uptime_get_32() - (uint32_t)atomic_get(&unplugged_at[half]) < SETTLE_MS;
}

bool dongle_half_temperature(int half, int8_t *celsius) {
    if (half < 0 || half >= 2 || !atomic_test_bit(&temp_known, half)) {
        return false;
    }
    *celsius = (int8_t)atomic_get(&temp_c[half]);
    return true;
}

uint16_t dongle_half_millivolts(int half) {
    return half >= 0 && half < 2 ? (uint16_t)atomic_get(&millivolts[half]) : 0;
}

bool dongle_half_tx_power(int half, int8_t *dbm) {
    if (half < 0 || half >= 2 || !atomic_test_bit(&tx_known, half)) {
        return false;
    }
    *dbm = (int8_t)atomic_get(&tx_dbm[half]);
    return true;
}

static const struct bt_uuid *uuid_of(int kind) {
    return kind == BLS        ? BT_UUID_BAS_BATTERY_LEVEL_STATUS
           : kind == TEMP     ? BT_UUID_TEMPERATURE
           : kind == TX_POWER ? BT_UUID_TPS_TX_POWER_LEVEL
                              : BT_UUID_GATT_V;
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

// Which watch a read or subscription belongs to.
static void find(const void *params, int *half, int *kind) {
    for (int h = 0; h < 2; h++) {
        for (int k = 0; k < KINDS; k++) {
            if (params == &watches[h][k].read || params == &watches[h][k].sub) {
                *half = h;
                *kind = k;
                return;
            }
        }
    }
    *half = *kind = -1;
}

/* --- the one request out at a time ------------------------------------------- */

static struct k_spinlock lock;
static struct bt_conn *conns[2]; // a reference to each half's link, under lock
static atomic_t ready;           // bit per half: ZMK is done with it
static atomic_t done[2];         // bit per kind: read (and subscribed), or given up
static atomic_t tries[2][KINDS];
static atomic_t not_before[2]; // uptime ms: that half waits for a retry until then
#define IDLE (-1)
static atomic_t inflight = ATOMIC_INIT(IDLE); // half * KINDS + kind, or IDLE
#define NOT_STUCK (-1)
static atomic_t stuck[2] = {ATOMIC_INIT(NOT_STUCK), ATOMIC_INIT(NOT_STUCK)}; // kind that timed out
static atomic_t last_err[2]; // ATT error, or 0x80 | errno; 0 for none

static void note_err(int half, int err) {
    if (err != 0) {
        atomic_set(&last_err[half], err < 0 ? 0x80 | MIN(-err, 0x7f) : MIN(err, 0x7f));
    }
}

static void step_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(step_work, step_cb);
static void step_soon(void) { k_work_reschedule(&step_work, K_NO_WAIT); }
static void timeout_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_cb);

// The one out has had no answer: leave its half alone until it reconnects.
static void timeout_cb(struct k_work *work) {
    const atomic_val_t id = atomic_get(&inflight);
    if (id == IDLE || !atomic_cas(&inflight, id, IDLE)) {
        return;
    }
    const int half = (int)id / KINDS;
    atomic_set(&stuck[half], id % KINDS);
    atomic_set(&done[half], BIT_MASK(KINDS));
    LOG_WRN("half %d: no answer to request %d", half, (int)id % KINDS);
    step_soon();
}

static const char kind_letter[KINDS] = {'B', 'C', 'X', 'V'};

void dongle_half_info_progress(int half, char code[3], uint8_t *err) {
    code[0] = code[1] = code[2] = '\0';
    *err = 0;
    if (half < 0 || half >= 2) {
        return;
    }
    *err = (uint8_t)atomic_get(&last_err[half]);
    const atomic_val_t id = atomic_get(&inflight);
    const atomic_val_t was_stuck = atomic_get(&stuck[half]);
    bool connected = false;
    K_SPINLOCK(&lock) {
        connected = conns[half] != NULL;
    }
    if (!connected) {
        return;
    }
    if (was_stuck != NOT_STUCK) {
        code[0] = 'T';
        code[1] = kind_letter[was_stuck];
    } else if (!atomic_test_bit(&ready, half)) {
        code[0] = 'W';
        code[1] = 'T';
    } else if (id != IDLE && id / KINDS == half) {
        code[0] = kind_letter[id % KINDS];
        code[1] = (char)('1' + MIN(atomic_get(&tries[half][id % KINDS]), 8));
    }
}

// The request for (half, kind) is over: done, or to be tried again later.
static void finish(int half, int kind, bool ok) {
    if (!atomic_cas(&inflight, half * KINDS + kind, IDLE)) {
        return; // not the one out (its half disconnected, or it timed out)
    }
    k_work_cancel_delayable(&timeout_work);
    if (ok || atomic_inc(&tries[half][kind]) + 1 >= RETRIES) {
        atomic_set_bit(&done[half], kind);
    } else {
        atomic_set(&not_before[half], (atomic_val_t)(k_uptime_get_32() + RETRY_S * 1000));
    }
    step_soon();
}

// A value, read or notified; NULL when there is none.
static void take(int half, int kind, const void *data, uint16_t length) {
    if (kind == BLS) {
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
    } else if (kind == VOLTS) {
        // in 1/64 V, as the characteristic has it
        const uint16_t sixty_fourths = data != NULL && length >= 2 ? sys_get_le16(data) : 0;
        atomic_set(&millivolts[half], (atomic_val_t)MIN((uint32_t)sixty_fourths * 1000 / 64, UINT16_MAX));
    } else if (kind == TX_POWER) {
        if (data != NULL && length >= 1) {
            atomic_set(&tx_dbm[half], *(const int8_t *)data);
        }
        atomic_set_bit_to(&tx_known, half, data != NULL && length >= 1);
    } else {
        const int16_t hundredths =
            data != NULL && length >= 2 ? (int16_t)sys_get_le16(data) : INT16_MIN;
        if (hundredths != INT16_MIN) {
            atomic_set(&temp_c[half], CLAMP(hundredths / 100, INT8_MIN, INT8_MAX));
        }
        atomic_set_bit_to(&temp_known, half, hundredths != INT16_MIN);
    }
}

static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length) {
    int half, kind;
    find(params, &half, &kind);
    if (half < 0) {
        return BT_GATT_ITER_STOP;
    }
    if (data == NULL) {
        // not subscribed (its CCC wasn't found), or no longer: the link has
        // gone (the subscription is volatile)
        if (atomic_get(&inflight) == half * KINDS + kind) {
            note_err(half, -ENOENT);
        }
        params->value_handle = 0;
        finish(half, kind, false);
        return BT_GATT_ITER_STOP;
    }
    take(half, kind, data, length);
    return BT_GATT_ITER_CONTINUE;
}

// The CCC written (or not).
static void subscribed_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params) {
    int half, kind;
    find(params, &half, &kind);
    if (half >= 0) {
        note_err(half, err);
        finish(half, kind, err == 0);
    }
}

static void subscribe(struct bt_conn *conn, int half, int kind, uint16_t value_handle) {
    struct watch *w = &watches[half][kind];
    memset(&w->sub, 0, sizeof(w->sub));
    w->sub.notify = notify_cb;
    w->sub.subscribe = subscribed_cb;
    w->sub.value = BT_GATT_CCC_NOTIFY;
    w->sub.value_handle = value_handle;
    w->sub.ccc_handle = 0; // found by Zephyr, from the value on
    w->sub.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    w->sub.disc_params = &w->disc;
    atomic_set_bit(w->sub.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE); // gone with the link
    k_work_reschedule(&timeout_work, K_SECONDS(REQUEST_TIMEOUT_S)); // a new request: its own time
    const int err = bt_gatt_subscribe(conn, &w->sub);
    if (err == -EALREADY) {
        finish(half, kind, true);
    } else if (err) {
        LOG_DBG("half %d: no subscription to %d (%d)", half, kind, err);
        note_err(half, err);
        finish(half, kind, false);
    }
    // else subscribed_cb, or notify_cb with no data, says how it went
}

// Called once per read: with the value (and its handle in by_uuid), or with
// no data after an error: no such characteristic (older firmware), or a
// failure worth trying again. Zephyr only calls again after a value if asked
// to go on.
static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                       const void *data, uint16_t length) {
    int half, kind;
    find(params, &half, &kind);
    if (half < 0) {
        return BT_GATT_ITER_STOP;
    }
    if (err && err != BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
        LOG_DBG("half %d: reading %d failed (0x%02x)", half, kind, err);
        note_err(half, err);
        finish(half, kind, false);
        return BT_GATT_ITER_STOP;
    }
    const bool found = !err && data != NULL;
    take(half, kind, found ? data : NULL, length);
    if (found && kind != TX_POWER && atomic_get(&inflight) == half * KINDS + kind) {
        subscribe(conn, half, kind, params->by_uuid.start_handle);
    } else {
        finish(half, kind, true);
    }
    return BT_GATT_ITER_STOP;
}

// On the system work queue: send the next request, if none is out.
static void step_cb(struct k_work *work) {
    if (atomic_get(&inflight) != IDLE) {
        return; // its end steps again
    }
    int32_t wait_ms = 0; // until a half waiting for a retry may go
    for (int half = 0; half < 2; half++) {
        const atomic_val_t todo = BIT_MASK(KINDS) & ~atomic_get(&done[half]);
        if (!atomic_test_bit(&ready, half) || todo == 0) {
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
        int kind = 0;
        while (!(todo & BIT(kind))) {
            kind++;
        }
        struct bt_gatt_read_params *p = &watches[half][kind].read;
        memset(p, 0, sizeof(*p));
        p->func = read_cb;
        p->handle_count = 0; // read by UUID
        p->by_uuid.uuid = uuid_of(kind);
        p->by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
        p->by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
        atomic_set(&inflight, half * KINDS + kind);
        k_work_reschedule(&timeout_work, K_SECONDS(REQUEST_TIMEOUT_S));
        const int err = bt_gatt_read(conn, p);
        bt_conn_unref(conn);
        if (err) {
            LOG_DBG("half %d: couldn't read %d (%d)", half, kind, err);
            note_err(half, err);
            finish(half, kind, false);
        }
        return; // one at a time
    }
    if (wait_ms > 0) {
        k_work_schedule(&step_work, K_MSEC(wait_ms));
    }
}

/* --- when to start: once ZMK is done with the half ----------------------------- */

static atomic_t awaiting_battery; // bit per half: connected, ZMK's first reading not yet in
static void ready_cb(struct k_work *work);
static struct k_work_delayable ready_work[2];

static void ready_cb(struct k_work *work) {
    const int half = k_work_delayable_from_work(work) == &ready_work[1] ? 1 : 0;
    atomic_clear_bit(&awaiting_battery, half);
    atomic_clear(&done[half]);
    for (int kind = 0; kind < KINDS; kind++) {
        atomic_clear(&tries[half][kind]);
    }
    atomic_clear(&not_before[half]);
    atomic_set(&stuck[half], NOT_STUCK);
    atomic_clear(&last_err[half]);
    atomic_set_bit(&ready, half);
    step_soon();
}

static int half_info_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL || ev->source >= 2) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    // a fresh reading: no longer the charger's
    atomic_clear_bit(&settling, ev->source);
    if (atomic_test_and_clear_bit(&awaiting_battery, ev->source)) {
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
    atomic_clear_bit(&ready, half);
    atomic_set(&stuck[half], NOT_STUCK);
    atomic_clear(&last_err[half]);
    atomic_set_bit(&awaiting_battery, half);
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
    atomic_clear_bit(&ready, half);
    atomic_clear_bit(&awaiting_battery, half);
    atomic_clear_bit(&charging, half);
    atomic_clear_bit(&settling, half);
    atomic_clear_bit(&temp_known, half);
    atomic_clear_bit(&tx_known, half);
    atomic_clear(&millivolts[half]);
    // a request out to it won't be answered: let the other half's go
    for (int kind = 0; kind < KINDS; kind++) {
        atomic_cas(&inflight, half * KINDS + kind, IDLE);
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
