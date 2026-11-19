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
 *
 * Each is read once, a few seconds after a half connects (a read by UUID
 * needs no finding first, so it doesn't get in the way of ZMK's own setup of
 * the link), and then subscribed to: the half sends it again only when it
 * changes, in a connection event it wakes for anyway. The transmit power is
 * fixed when the half's firmware is built, so it is only read. Nothing is
 * polled, so it costs the halves' batteries next to nothing. A half with
 * firmware from before this has none of them: it reads as not charging,
 * temperature and transmit power unknown.
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

#include "dongle_half_info.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Battery Level Status: flags (1 byte), then power state (2, little endian),
// whose bits 5-6 are the charge state (Bluetooth's GATT Specification
// Supplement; Zephyr's subsys/bluetooth/services/bas/bas_bls.c).
#define POWER_STATE_CHARGE_SHIFT 5
#define POWER_STATE_CHARGE_MASK (BIT_MASK(2) << POWER_STATE_CHARGE_SHIFT)
#define CHARGE_STATE_CHARGING 1

enum { BLS, TEMP, TX_POWER, KINDS };

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

bool dongle_half_charging(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&charging, half);
}

bool dongle_half_temperature(int half, int8_t *celsius) {
    if (half < 0 || half >= 2 || !atomic_test_bit(&temp_known, half)) {
        return false;
    }
    *celsius = (int8_t)atomic_get(&temp_c[half]);
    return true;
}

bool dongle_half_tx_power(int half, int8_t *dbm) {
    if (half < 0 || half >= 2 || !atomic_test_bit(&tx_known, half)) {
        return false;
    }
    *dbm = (int8_t)atomic_get(&tx_dbm[half]);
    return true;
}

static const struct bt_uuid *uuid_of(int kind) {
    return kind == BLS ? BT_UUID_BAS_BATTERY_LEVEL_STATUS : kind == TEMP ? BT_UUID_TEMPERATURE
                                                                         : BT_UUID_TPS_TX_POWER_LEVEL;
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

// A value, read or notified; NULL when there is none.
static void take(int half, int kind, const void *data, uint16_t length) {
    if (kind == BLS) {
        bool on = false;
        if (data != NULL && length >= 3) {
            const uint16_t power = sys_get_le16((const uint8_t *)data + 1);
            on = FIELD_GET(POWER_STATE_CHARGE_MASK, power) == CHARGE_STATE_CHARGING;
        }
        atomic_set_bit_to(&charging, half, on);
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
        // unsubscribed: the link has gone (the subscription is volatile)
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }
    take(half, kind, data, length);
    return BT_GATT_ITER_CONTINUE;
}

static void subscribe(struct bt_conn *conn, int half, int kind, uint16_t value_handle) {
    struct watch *w = &watches[half][kind];
    memset(&w->sub, 0, sizeof(w->sub));
    w->sub.notify = notify_cb;
    w->sub.value = BT_GATT_CCC_NOTIFY;
    w->sub.value_handle = value_handle;
    w->sub.ccc_handle = 0; // found by Zephyr, from the value on
    w->sub.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    w->sub.disc_params = &w->disc;
    atomic_set_bit(w->sub.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE); // gone with the link
    const int err = bt_gatt_subscribe(conn, &w->sub);
    if (err && err != -EALREADY) {
        LOG_DBG("half %d: no subscription to %d (%d)", half, kind, err);
    }
}

// Called once per read: with the value (and its handle in by_uuid), or with
// no data after an error, most likely no such characteristic (older
// firmware). Zephyr only calls again after a value if asked to go on.
static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                       const void *data, uint16_t length) {
    int half, kind;
    find(params, &half, &kind);
    if (half < 0) {
        return BT_GATT_ITER_STOP;
    }
    const bool found = !err && data != NULL;
    take(half, kind, found ? data : NULL, length);
    if (found && kind != TX_POWER) {
        subscribe(conn, half, kind, params->by_uuid.start_handle);
    }
    return BT_GATT_ITER_STOP;
}

static void start(struct bt_conn *conn, int half) {
    for (int kind = 0; kind < KINDS; kind++) {
        struct bt_gatt_read_params *p = &watches[half][kind].read;
        memset(p, 0, sizeof(*p));
        p->func = read_cb;
        p->handle_count = 0; // read by UUID
        p->by_uuid.uuid = uuid_of(kind);
        p->by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
        p->by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
        // queued one after the other by Zephyr's ATT
        if (bt_gatt_read(conn, p) != 0) {
            LOG_DBG("half %d: couldn't read %d", half, kind);
        }
    }
}

// A few seconds after a half connects: by then ZMK has set the link up.
static atomic_t pending; // bit per half
static void start_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(start_work, start_cb);

static void start_one(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    const int half = half_of(conn);
    if (half >= 0 && atomic_test_and_clear_bit(&pending, half)) {
        start(conn, half);
    }
}

static void start_cb(struct k_work *work) { bt_conn_foreach(BT_CONN_TYPE_LE, start_one, NULL); }

static void connected(struct bt_conn *conn, uint8_t err) {
    const int half = err ? -1 : half_of(conn);
    if (half >= 0) {
        atomic_set_bit(&pending, half);
        k_work_reschedule(&start_work, K_SECONDS(3));
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    const int half = half_of(conn);
    if (half >= 0) {
        atomic_clear_bit(&pending, half);
        atomic_clear_bit(&charging, half);
        atomic_clear_bit(&temp_known, half);
        atomic_clear_bit(&tx_known, half);
    }
}

BT_CONN_CB_DEFINE(dongle_half_info_cb) = {
    .connected = connected,
    .disconnected = disconnected,
};
