/*
 * A half behind the dongle: the power its radio transmits at, for the
 * dongle's Debug screen (the TX column), so a board built with more (e.g.
 * CONFIG_BT_CTLR_TX_PWR_PLUS_8) shows it. Offered as the Tx Power Level
 * (dBm, signed) of Bluetooth's standard Tx Power Service. It is fixed when
 * the firmware is built, so there is nothing to measure or notify: the
 * dongle reads it once when the half connects (src/dongle_half_info.c).
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

static ssize_t read_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset) {
    const int8_t dbm = CONFIG_BT_CTLR_TX_PWR_DBM;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &dbm, sizeof(dbm));
}

BT_GATT_SERVICE_DEFINE(half_tx_power_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_TPS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_TPS_TX_POWER_LEVEL, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
                                              read_tx_power, NULL, NULL));
