/*
 * A half behind the dongle: its battery's voltage, for the dongle's Debug
 * screen (the V column). Offered as the Voltage characteristic (1/64 V,
 * unsigned) of Bluetooth's Environmental Sensing Service, in a service of
 * its own so it doesn't depend on src/half_temperature.c.
 *
 * Nothing extra is measured: ZMK measures the battery every
 * ZMK_BATTERY_REPORT_INTERVAL for its percentage (and again after unplugging,
 * src/battery_refresh.c), and this takes the last of those from the battery
 * sensor without asking for a new one. Looked at every VOLTAGE_EVERY_S and
 * notified only when it has moved by NOTIFY_STEP (about 30 mV), in a
 * connection event the radio would wake for anyway. While on USB power it
 * is the charger's, about 4.2 V. The dongle reads it once when the half is
 * set up and subscribes (src/dongle_half_info.c).
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#define VOLTAGE_EVERY_S 20
#define NOTIFY_STEP 2 // in 1/64 V

static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static uint16_t sixty_fourths; // 0: not known yet

static uint16_t last_reading(void) {
    struct sensor_value v;
    if (!device_is_ready(battery) || sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &v) != 0) {
        return 0;
    }
    // ZMK's battery drivers give millivolts in val1
    const int32_t mv = v.val1 > 100 ? v.val1 : v.val1 * 1000 + v.val2 / 1000;
    return mv > 0 ? (uint16_t)MIN(mv * 64 / 1000, UINT16_MAX) : 0;
}

static ssize_t read_voltage(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                            uint16_t offset) {
    if (sixty_fourths == 0) {
        sixty_fourths = last_reading();
    }
    uint8_t value[2];
    sys_put_le16(sixty_fourths, value);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

BT_GATT_SERVICE_DEFINE(half_voltage_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_V, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_voltage, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void look_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(look_work, look_cb);

static void look_cb(struct k_work *work) {
    const uint16_t now = last_reading();
    if (now != 0 && abs((int)now - (int)sixty_fourths) >= NOTIFY_STEP) {
        sixty_fourths = now;
        uint8_t value[2];
        sys_put_le16(sixty_fourths, value);
        // to whoever has subscribed (the dongle); nothing goes out otherwise
        bt_gatt_notify(NULL, &half_voltage_svc.attrs[2], value, sizeof(value));
    }
    k_work_schedule(&look_work, K_SECONDS(VOLTAGE_EVERY_S));
}

static int half_voltage_init(void) {
    k_work_schedule(&look_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(half_voltage_init, APPLICATION, 99);
