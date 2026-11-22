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
 * sensor every VOLTAGE_EVERY_S. A single reading jitters by a few millivolts
 * and dips while the radio sends, far more than an hour's draw moves it, so
 * what is sent is the average of the last VOLTAGE_AVERAGE_N (started again
 * when USB power comes or goes), whenever that has moved by a millivolt, as a
 * notification in a connection event the radio wakes for anyway. The dongle already subscribes to it for the charging state,
 * so this asks nothing more of the link.
 *
 * A half whose charger has a status output (CONFIG_HALF_CHARGER_STATUS_PIN,
 * the Forager's) reads it every CHARGER_EVERY_S while on USB power, and sends
 * the level it reads in the same characteristic's Charge Type, also ours to
 * use: "constant current" for low, "float" for high (the Forager's BQ25101
 * reportedly holds it low while charging, so high once it has finished),
 * "unknown" off USB power or with no such pin. Off USB power the pin is left
 * unconnected, as reading it then reportedly costs about 400 uA.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
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

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
static atomic_t voltage_restart; // USB power came or went: the voltage average starts again
#endif

#if CONFIG_HALF_CHARGER_STATUS_PIN >= 0
#define CHARGER_PIN CONFIG_HALF_CHARGER_STATUS_PIN
#define CHARGER_EVERY_S 30
#define CHARGER_AFTER_USB_S 2 // once USB power comes, for the charger to start

static const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static void charger_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(charger_work, charger_cb);

static void charger_cb(struct k_work *work) {
    static enum bt_bas_bls_battery_charge_type sent = BT_BAS_BLS_CHARGE_TYPE_UNKNOWN;
    enum bt_bas_bls_battery_charge_type type = BT_BAS_BLS_CHARGE_TYPE_UNKNOWN;
    if (!device_is_ready(gpio0)) {
        return;
    }
    if (zmk_usb_is_powered()) {
        gpio_pin_configure(gpio0, CHARGER_PIN, GPIO_INPUT | GPIO_PULL_UP);
        k_busy_wait(100); // for the pull-up
        const int level = gpio_pin_get_raw(gpio0, CHARGER_PIN);
        if (level >= 0) {
            type = level ? BT_BAS_BLS_CHARGE_TYPE_FLOAT : BT_BAS_BLS_CHARGE_TYPE_CONSTANT_CURRENT;
        }
        k_work_schedule(&charger_work, K_SECONDS(CHARGER_EVERY_S));
    } else {
        gpio_pin_configure(gpio0, CHARGER_PIN, GPIO_DISCONNECTED);
    }
    if (type != sent) {
        sent = type;
        bt_bas_bls_set_battery_charge_type(type);
    }
}
#endif

static int half_charging_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    atomic_set(&voltage_restart, 1);
#endif
    half_charging_update();
#if CONFIG_HALF_CHARGER_STATUS_PIN >= 0
    k_work_reschedule(&charger_work, K_SECONDS(CHARGER_AFTER_USB_S));
#endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_charging, half_charging_listener);
ZMK_SUBSCRIPTION(half_charging, zmk_usb_conn_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
#define VOLTAGE_EVERY_S 60
#define VOLTAGE_AVERAGE_N 16

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
    static uint16_t readings[VOLTAGE_AVERAGE_N], sent;
    static int count, next;
    if (atomic_clear(&voltage_restart)) {
        count = next = 0;
    }
    const uint16_t mv = last_millivolts();
    if (mv != 0) {
        readings[next] = mv;
        next = (next + 1) % VOLTAGE_AVERAGE_N;
        count = MIN(count + 1, VOLTAGE_AVERAGE_N);
        uint32_t sum = 0;
        for (int i = 0; i < count; i++) {
            sum += readings[i];
        }
        const uint16_t average = (uint16_t)((sum + count / 2) / count);
        if (average != sent) {
            sent = average;
            bt_bas_bls_set_identifier(average);
        }
    }
    k_work_schedule(&voltage_work, K_SECONDS(VOLTAGE_EVERY_S));
}
#endif

static int half_charging_init(void) {
    half_charging_update();
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    k_work_schedule(&voltage_work, K_SECONDS(5)); // after ZMK's first reading
#endif
#if CONFIG_HALF_CHARGER_STATUS_PIN >= 0
    k_work_schedule(&charger_work, K_SECONDS(CHARGER_AFTER_USB_S));
#endif
    return 0;
}

// after USB (ZMK_USB_INIT_PRIORITY) has had its first look at VBUS
SYS_INIT(half_charging_init, APPLICATION, 99);
