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
 * millivolts). Nothing extra is measured: ZMK reads the battery once a
 * minute for its percentage, and this takes that reading every
 * TAKE_EVERY_S and sends it when it has changed, as a notification in a
 * connection event the radio wakes for anyway. The dongle already
 * subscribes to it for the charging state, so this asks nothing more of the
 * link. The dongle fits a line through them all, which averages them, so
 * nothing is averaged here.
 *
 * ZMK's nRF VDDH driver (the nice!nano) rounds its reading to whole
 * millivolts of the ADC's input before multiplying by 5, so its steps are
 * 5 mV. The ADC's own value, which the driver keeps, is finer: 12 bits of
 * 1.2 V, times 5, steps of about 1.46 mV. So on those boards the reading is
 * worked out again from that value (see fine_tenths), and checked against
 * ZMK's millivolts; other drivers' (about 3 mV steps on a XIAO) are sent as
 * they are.
 *
 * Reading the ADC here as well, at 14 bits with 64 samples in burst mode,
 * gave finer readings but drained both halves many times faster (builds
 * 343 to 355, even at one reading a minute), possibly the nRF52's SAADC
 * erratum 241 (a static current after burst sampling): leave the ADC to ZMK.
 */

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
#define TAKE_EVERY_S 60 // ZMK's reading taken this often (ZMK reads once a minute)
#define CHECK_TENTHS 60 // further than this from ZMK's millivolts: ZMK's is sent

#define BATTERY DT_CHOSEN(zmk_battery)

static const struct device *const battery = DEVICE_DT_GET(BATTERY);

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

#if DT_NODE_HAS_COMPAT(BATTERY, zmk_battery_nrf_vddh)
#include <zephyr/drivers/adc.h>

/* ZMK's nRF VDDH driver's data (app/module/drivers/sensor/battery/
 * battery_nrf_vddh.c and battery_common.h, at the ZMK pinned in
 * config/west.yml), to read the ADC value it keeps. If ZMK changes it, the
 * check against its millivolts fails and those are sent instead. */
struct zmk_vddh_data {
    struct adc_channel_cfg acc;
    struct adc_sequence as;
    struct {
        uint16_t adc_raw;
        uint16_t millivolts;
        uint8_t state_of_charge;
    } value;
};

#define VDDH_RESOLUTION 12               // as ZMK's driver reads it
#define VDDH_FULL_SCALE_MV (600 * 2 * 5) // 0.6 V reference, gain 1/2, VDDH/5

// ZMK's last reading in tenths of a millivolt, from the ADC's value; 0 when it doesn't check out.
static uint32_t fine_tenths(uint16_t zmk_mv) {
    const struct zmk_vddh_data *data = battery->data;
    const int16_t raw = (int16_t)data->value.adc_raw;
    if (raw <= 0 || data->as.resolution != VDDH_RESOLUTION) {
        return 0;
    }
    const uint32_t tenths = ((uint32_t)raw * VDDH_FULL_SCALE_MV * 10 + (1U << (VDDH_RESOLUTION - 1))) >>
                            VDDH_RESOLUTION;
    const uint32_t zmk = zmk_mv * 10U;
    return (tenths > zmk ? tenths - zmk : zmk - tenths) <= CHECK_TENTHS ? tenths : 0;
}
#else
static uint32_t fine_tenths(uint16_t zmk_mv) { return 0; }
#endif

static void voltage_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(voltage_work, voltage_cb);

static void voltage_cb(struct k_work *work) {
    static uint16_t sent;
    const uint16_t mv = last_millivolts();
    if (mv != 0) {
        const uint32_t fine = fine_tenths(mv);
        const uint16_t tenths = (uint16_t)MIN(fine ? fine : mv * 10U, UINT16_MAX);
        if (tenths != sent) {
            sent = tenths;
            bt_bas_bls_set_identifier(tenths);
        }
    }
    k_work_schedule(&voltage_work, K_SECONDS(TAKE_EVERY_S));
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
