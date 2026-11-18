/*
 * Prospector dongle: whether each half is on USB power, read from its battery
 * service's Battery Level Status (set by src/half_charging.c), for the
 * screens to show a bolt for that half instead of its level.
 *
 * Read, not subscribed to: ZMK finds its own characteristics on the halves'
 * connections, and a read by UUID needs no finding first. Each connected
 * half is asked every DONGLE_HALF_CHARGING_POLL_S seconds, one small
 * request that rides on the link's normal traffic. A half with firmware from
 * before this has no such characteristic: it reads as not charging.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
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

#include "dongle_half_charging.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Battery Level Status: flags (1 byte), then power state (2, little endian),
// whose bits 5-6 are the charge state (Bluetooth's GATT Specification
// Supplement; Zephyr's subsys/bluetooth/services/bas/bas_bls.c).
#define POWER_STATE_CHARGE_SHIFT 5
#define POWER_STATE_CHARGE_MASK (BIT_MASK(2) << POWER_STATE_CHARGE_SHIFT)
#define CHARGE_STATE_CHARGING 1

static atomic_t charging;  // bit per half
static atomic_t in_flight; // bit per half: a read is out
static struct bt_gatt_read_params params[2];

bool dongle_half_charging(int half) {
    return half >= 0 && half < 2 && atomic_test_bit(&charging, half);
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

// Called once: with the value, or with no data after an error (Zephyr only
// calls again after a value if this asks it to go on, and it doesn't).
static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *p,
                       const void *data, uint16_t length) {
    const int half = p == &params[1];
    bool on = false; // an error is most likely no such characteristic: older firmware
    if (!err && data != NULL && length >= 3) {
        const uint16_t power = sys_get_le16((const uint8_t *)data + 1);
        on = FIELD_GET(POWER_STATE_CHARGE_MASK, power) == CHARGE_STATE_CHARGING;
    }
    atomic_set_bit_to(&charging, half, on);
    atomic_clear_bit(&in_flight, half);
    return BT_GATT_ITER_STOP;
}

static void poll_one(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    const int half = half_of(conn);
    if (half < 0 || atomic_test_and_set_bit(&in_flight, half)) {
        return;
    }
    struct bt_gatt_read_params *p = &params[half];
    memset(p, 0, sizeof(*p));
    p->func = read_cb;
    p->handle_count = 0; // read by UUID
    p->by_uuid.uuid = BT_UUID_BAS_BATTERY_LEVEL_STATUS;
    p->by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    p->by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    if (bt_gatt_read(conn, p) != 0) {
        atomic_clear_bit(&in_flight, half);
    }
}

static void poll_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_cb);

static void poll_cb(struct k_work *work) {
    bt_conn_foreach(BT_CONN_TYPE_LE, poll_one, NULL);
    k_work_schedule(&poll_work, K_SECONDS(CONFIG_DONGLE_HALF_CHARGING_POLL_S));
}

static void connected(struct bt_conn *conn, uint8_t err) {
    // a first look soon after a half connects, once ZMK has set the link up
    const int half = err ? -1 : half_of(conn);
    if (half >= 0) {
        atomic_clear_bit(&in_flight, half); // in case a read was lost with the old link
        k_work_reschedule(&poll_work, K_SECONDS(3));
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    const int half = half_of(conn);
    if (half >= 0) {
        atomic_clear_bit(&charging, half);
    }
}

BT_CONN_CB_DEFINE(dongle_half_charging_cb) = {
    .connected = connected,
    .disconnected = disconnected,
};

static int dongle_half_charging_init(void) {
    k_work_schedule(&poll_work, K_SECONDS(CONFIG_DONGLE_HALF_CHARGING_POLL_S));
    return 0;
}

SYS_INIT(dongle_half_charging_init, APPLICATION, 99);
