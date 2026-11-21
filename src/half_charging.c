/*
 * A half behind the dongle: says whether it is on USB power, for the dongle's
 * screens, which show a bolt for it then instead of a battery level (the level
 * a half reads while charging is the charger's voltage, near 100 whatever the
 * charge). The left half's own nice!view does the same from zmk_usb_is_powered.
 *
 * It uses the Battery Level Status characteristic of Bluetooth's standard
 * battery service, which ZMK already runs on every half for its level: the
 * charge state "charging" on USB power, "discharging" off it. The dongle
 * reads it (src/dongle_half_info.c). A half doesn't know when its charger
 * has finished, so "charging" means only "plugged in". After unplugging it
 * stays "charging" until the battery has been read again
 * (src/battery_refresh.c), so the dongle never shows the charger's reading.
 *
 * The same characteristic carries the battery's voltage, for the dongle's
 * figure of each half's draw (src/dongle_half_drain.c): in its optional
 * Identifier field, which is ours to use, in millivolts. Nothing extra is
 * measured: ZMK reads the battery every ZMK_BATTERY_REPORT_INTERVAL for its
 * percentage, and this takes the last of those readings from the battery
 * sensor. Looked at every VOLTAGE_EVERY_S, sent only when it has moved by
 * VOLTAGE_STEP_MV, as a notification in a connection event the radio wakes
 * for anyway. The dongle already subscribes to it for the charging state,
 * so this asks nothing more of the link.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include "battery_refresh.h"
#include "half_status.h"

void half_charging_update(void) {
#if IS_ENABLED(CONFIG_BATTERY_REFRESH_ON_UNPLUG)
    const bool usb = battery_refresh_shows_usb_power();
#else
    const bool usb = zmk_usb_is_powered();
#endif
    bt_bas_bls_set_battery_present(BT_BAS_BLS_BATTERY_PRESENT);
    bt_bas_bls_set_wired_external_power_source(usb ? BT_BAS_BLS_WIRED_POWER_CONNECTED
                                                   : BT_BAS_BLS_WIRED_POWER_NOT_CONNECTED);
    bt_bas_bls_set_battery_charge_state(usb ? BT_BAS_BLS_CHARGE_STATE_CHARGING
                                            : BT_BAS_BLS_CHARGE_STATE_DISCHARGING_ACTIVE);
}

static int half_charging_listener(const zmk_event_t *eh) {
    half_charging_update();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_charging, half_charging_listener);
ZMK_SUBSCRIPTION(half_charging, zmk_usb_conn_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
#define VOLTAGE_EVERY_S 60
#define VOLTAGE_STEP_MV 4

static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

// ZMK's last battery reading in millivolts, without asking for a new one; 0 when there is none.
static uint16_t last_millivolts(void) {
    struct sensor_value v;
    if (!device_is_ready(battery) || sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &v) != 0) {
        return 0;
    }
    // ZMK's battery drivers differ: volts and microvolts, or millivolts in val1
    const int32_t mv = v.val1 > 100 ? v.val1 : v.val1 * 1000 + v.val2 / 1000;
    return mv > 0 ? (uint16_t)MIN(mv, UINT16_MAX) : 0;
}

static void voltage_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(voltage_work, voltage_cb);

static void voltage_cb(struct k_work *work) {
    static uint16_t sent;
    const uint16_t mv = last_millivolts();
    if (mv != 0 && abs((int)mv - (int)sent) >= VOLTAGE_STEP_MV) {
        sent = mv;
        bt_bas_bls_set_identifier(mv);
    }
    k_work_schedule(&voltage_work, K_SECONDS(VOLTAGE_EVERY_S));
}
#endif

static int half_charging_init(void) {
    half_charging_update();
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    k_work_schedule(&voltage_work, K_SECONDS(5)); // after ZMK's first reading
#endif
    return 0;
}

// after USB (ZMK_USB_INIT_PRIORITY) has had its first look at VBUS
SYS_INIT(half_charging_init, APPLICATION, 99);
