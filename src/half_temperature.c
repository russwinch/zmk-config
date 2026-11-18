/*
 * A half behind the dongle: its chip's temperature, for the dongle's Debug
 * screen. Offered as the Temperature characteristic (0.01 degrees C, signed)
 * of Bluetooth's standard Environmental Sensing Service.
 *
 * Cheap on the battery: measured every HALF_TEMPERATURE_EVERY_S (the TEMP
 * peripheral, some tens of microseconds) and notified only when it has moved
 * by a whole degree, which goes out in a connection event the radio would
 * wake for anyway. The dongle reads it once when it connects
 * (src/dongle_half_info.c). It is the nRF52840's die: a little above the room.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

static const struct device *const temp_dev = DEVICE_DT_GET(DT_NODELABEL(temp));

static int16_t hundredths = INT16_MIN; // "unknown" by the characteristic's definition

static int16_t measure(void) {
    struct sensor_value v;
    if (!device_is_ready(temp_dev) || sensor_sample_fetch(temp_dev) != 0 ||
        sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &v) != 0) {
        return INT16_MIN;
    }
    return (int16_t)(v.val1 * 100 + v.val2 / 10000);
}

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset) {
    uint8_t value[2];
    sys_put_le16((uint16_t)hundredths, value);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

BT_GATT_SERVICE_DEFINE(half_temperature_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_temperature, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void measure_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(measure_work, measure_work_cb);

static void measure_work_cb(struct k_work *work) {
    const int16_t now = measure();
    const bool moved = now != INT16_MIN &&
                       (hundredths == INT16_MIN || abs(now - hundredths) >= 100);
    if (moved) {
        hundredths = now;
        uint8_t value[2];
        sys_put_le16((uint16_t)hundredths, value);
        // to whoever has subscribed (the dongle); nothing goes out otherwise
        bt_gatt_notify(NULL, &half_temperature_svc.attrs[2], value, sizeof(value));
    }
    k_work_schedule(&measure_work, K_SECONDS(CONFIG_HALF_TEMPERATURE_EVERY_S));
}

static int half_temperature_init(void) {
    k_work_schedule(&measure_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(half_temperature_init, APPLICATION, 99);
