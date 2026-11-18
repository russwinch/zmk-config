/*
 * A half behind the dongle: its chip's temperature, for the dongle's Debug
 * screen. Offered as the Temperature characteristic (0.01 degrees C, signed)
 * of Bluetooth's standard Environmental Sensing Service, measured when it is
 * read (src/dongle_half_info.c reads it every half minute), so the half does
 * nothing in between. It is the nRF52840's die: a little above the room.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

static const struct device *const temp_dev = DEVICE_DT_GET(DT_NODELABEL(temp));

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset) {
    int16_t hundredths = INT16_MIN; // "unknown" by the characteristic's definition
    struct sensor_value v;
    if (device_is_ready(temp_dev) && sensor_sample_fetch(temp_dev) == 0 &&
        sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v) == 0) {
        hundredths = (int16_t)(v.val1 * 100 + v.val2 / 10000);
    }
    uint8_t value[2];
    sys_put_le16((uint16_t)hundredths, value);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

BT_GATT_SERVICE_DEFINE(half_temperature_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_temperature, NULL, NULL));
