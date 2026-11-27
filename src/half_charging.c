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
 * Identifier field, which is ours to use, in tenths of a millivolt (always
 * 10000 or more, so the dongle can tell it from older halves' whole
 * millivolts). The draw moves the voltage only a fraction of a millivolt an
 * hour, so the battery is read here every READ_EVERY_S, finer than ZMK reads
 * it (see own_tenths), never within QUIET_MS of a key (the radio's dip), and
 * what is sent is the average of the last AVERAGE_N (started again when USB
 * power comes or goes), at most every 5 minutes and only when it has moved, as
 * a notification in a connection event the radio wakes for anyway. The
 * dongle already subscribes to it for the charging state, so this asks
 * nothing more of the link.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/workqueue.h>
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

static int half_charging_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    atomic_set(&voltage_restart, 1);
#endif
    half_charging_update();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_charging, half_charging_listener);
ZMK_SUBSCRIPTION(half_charging, zmk_usb_conn_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
#define READ_EVERY_S 60    // a reading this often
#define AVERAGE_N 16       // averaged over the last this many (16 minutes)
#define SEND_EVERY_N 5     // and sent at most every 5 minutes
#define QUIET_MS 3000      // only this long after the last key: none while typing
#define CHECK_MV 100       // further than this from ZMK's own reading: ZMK's is used
#define OVERSAMPLING 6     // 2^6 conversions averaged by the ADC for each reading
#define RESOLUTION 14

#define BATTERY DT_CHOSEN(zmk_battery)

static const struct device *const battery = DEVICE_DT_GET(BATTERY);

// ZMK's last battery reading in millivolts, without asking for a new one; 0 when there is none.
static uint16_t zmk_millivolts(void) {
    struct sensor_value v;
    if (!device_is_ready(battery) || sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &v) != 0) {
        return 0;
    }
    // ZMK's battery drivers differ: volts and microvolts, or millivolts in val1
    const int32_t mv = v.val1 > 100 ? v.val1 : v.val1 * 1000 + v.val2 / 1000;
    return mv > 0 ? (uint16_t)MIN(mv, UINT16_MAX) : 0;
}

/* ZMK's drivers keep whole millivolts of the ADC's input, then scale them up:
 * steps of 5 mV on a nice!nano (VDDH/5), about 3 on a XIAO (its divider).
 * So the battery is read here from the same ADC channel, set up by ZMK's
 * driver (channel 0 of the SAADC, internal 0.6 V reference), at 14 bits
 * with the ADC averaging 64 conversions, and kept in tenths of a millivolt.
 * Run on ZMK's low priority queue, as ZMK's own reading is, so the two never
 * overlap (a XIAO's divider is switched on only for a reading). */
#if IS_ENABLED(CONFIG_ADC_NRFX_SAADC) && DT_NODE_HAS_COMPAT(BATTERY, zmk_battery_nrf_vddh)
#define OWN_READING 1
#define ADC_NODE DT_NODELABEL(adc)
#define SCALE_NUM (600ULL * 2 * 5) // mV at full scale: gain 1/2, then VDDH/5
#define SCALE_DEN 1ULL
#elif IS_ENABLED(CONFIG_ADC_NRFX_SAADC) && DT_NODE_HAS_COMPAT(BATTERY, zmk_battery_voltage_divider)
#define OWN_READING 1
#define ADC_NODE DT_IO_CHANNELS_CTLR(BATTERY)
#define SCALE_NUM (600ULL * 6 * DT_PROP(BATTERY, full_ohms)) // gain 1/6, then the divider
#define SCALE_DEN ((unsigned long long)DT_PROP(BATTERY, output_ohms))
#define HAS_POWER_GPIO DT_NODE_HAS_PROP(BATTERY, power_gpios)
#else
#define OWN_READING 0
#endif

#if OWN_READING
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

static const struct device *const adc = DEVICE_DT_GET(ADC_NODE);
#if defined(HAS_POWER_GPIO) && HAS_POWER_GPIO
static const struct gpio_dt_spec power = GPIO_DT_SPEC_GET(BATTERY, power_gpios);
#endif

// The battery now, in tenths of a millivolt; 0 when it couldn't be read.
static uint32_t own_tenths(void) {
    if (!device_is_ready(adc)) {
        return 0;
    }
    int16_t raw = 0;
    const struct adc_sequence seq = {
        .channels = BIT(0),
        .buffer = &raw,
        .buffer_size = sizeof(raw),
        .resolution = RESOLUTION,
        .oversampling = OVERSAMPLING,
    };
#if defined(HAS_POWER_GPIO) && HAS_POWER_GPIO
    if (gpio_pin_set_dt(&power, 1) != 0) {
        return 0;
    }
    k_sleep(K_MSEC(10)); // as ZMK's driver waits, for the divider to settle
#endif
    const int rc = adc_read(adc, &seq);
#if defined(HAS_POWER_GPIO) && HAS_POWER_GPIO
    gpio_pin_set_dt(&power, 0);
#endif
    if (rc != 0 || raw <= 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)raw * SCALE_NUM * 10 + (SCALE_DEN << (RESOLUTION - 1))) /
                      (SCALE_DEN << RESOLUTION));
}
#endif

// The battery now, in tenths of a millivolt; 0 when not known.
static uint32_t read_tenths(void) {
    const uint32_t zmk = zmk_millivolts() * 10U;
#if OWN_READING
    const uint32_t own = own_tenths();
    // a sanity check on the scaling above: ZMK's (up to a minute old) should be close
    if (own != 0 && (zmk == 0 || (own > zmk ? own - zmk : zmk - own) <= CHECK_MV * 10)) {
        return own;
    }
#endif
    return zmk;
}

static atomic_t last_key_ms; // uptime of the last key pressed or released

static int half_charging_key_listener(const zmk_event_t *eh) {
    atomic_set(&last_key_ms, (atomic_val_t)k_uptime_get_32());
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_charging_key, half_charging_key_listener);
ZMK_SUBSCRIPTION(half_charging_key, zmk_position_state_changed);

static void voltage_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(voltage_work, voltage_cb);

static void voltage_cb(struct k_work *work) {
    static uint16_t readings[AVERAGE_N], sent;
    static int count, next, since_sent = SEND_EVERY_N;
    if (atomic_clear(&voltage_restart)) {
        count = next = 0;
    }
    const uint32_t quiet = k_uptime_get_32() - (uint32_t)atomic_get(&last_key_ms);
    if (quiet < QUIET_MS) {
        // a key just now: the battery dips while the radio sends it, so wait
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &voltage_work,
                                  K_MSEC(QUIET_MS - quiet));
        return;
    }
    const uint32_t tenths = read_tenths();
    if (tenths != 0) {
        readings[next] = (uint16_t)MIN(tenths, UINT16_MAX);
        next = (next + 1) % AVERAGE_N;
        count = MIN(count + 1, AVERAGE_N);
    }
    since_sent++;
    if (count > 0 && since_sent >= SEND_EVERY_N) {
        uint32_t sum = 0;
        for (int i = 0; i < count; i++) {
            sum += readings[i];
        }
        const uint16_t average = (uint16_t)((sum + count / 2) / count);
        if (average != sent) {
            sent = average;
            since_sent = 0;
            bt_bas_bls_set_identifier(average);
        }
    }
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &voltage_work, K_SECONDS(READ_EVERY_S));
}
#endif

static int half_charging_init(void) {
    half_charging_update();
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    // after ZMK's first reading, which calibrates the ADC
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &voltage_work, K_SECONDS(5));
#endif
    return 0;
}

// after USB (ZMK_USB_INIT_PRIORITY) has had its first look at VBUS
SYS_INIT(half_charging_init, APPLICATION, 99);
