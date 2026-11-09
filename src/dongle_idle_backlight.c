/*
 * Prospector dongle: dim the screen backlight when the keyboard goes idle,
 * switch it off a while later, and restore it on the next key press.
 *
 * Only built with a fixed brightness (no ambient light sensor), since the
 * Prospector module's sensor thread would otherwise keep overriding it.
 * Settings: Kconfig at the repo root, values in config/waterfowl_dongle.conf.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// the backlight is the Prospector shield's pwm-leds child `disp_bl`
static const struct device *backlight = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
#define BACKLIGHT_IDX DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

static void set_backlight(uint8_t percent) {
    if (!device_is_ready(backlight)) {
        return;
    }
    int ret = led_set_brightness(backlight, BACKLIGHT_IDX, percent);
    if (ret < 0) {
        LOG_WRN("Could not set backlight to %d%% (%d)", percent, ret);
    }
}

static void backlight_off_cb(struct k_work *work) { set_backlight(0); }

static K_WORK_DELAYABLE_DEFINE(backlight_off_work, backlight_off_cb);

static int idle_backlight_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        k_work_cancel_delayable(&backlight_off_work);
        set_backlight(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
        break;
    case ZMK_ACTIVITY_IDLE:
        set_backlight(CONFIG_WATERFOWL_DONGLE_IDLE_BRIGHTNESS);
        if (CONFIG_WATERFOWL_DONGLE_BACKLIGHT_OFF_MS > 0) {
            k_work_schedule(&backlight_off_work, K_MSEC(CONFIG_WATERFOWL_DONGLE_BACKLIGHT_OFF_MS));
        }
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_idle_backlight, idle_backlight_listener);
ZMK_SUBSCRIPTION(dongle_idle_backlight, zmk_activity_state_changed);
