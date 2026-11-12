/*
 * Prospector dongle: dim the screen backlight when the keyboard goes idle,
 * switch it off a while later, and restore it on the next key press.
 *
 * "0%" never really sets a 0% duty cycle: Zephyr's nRF PWM driver stops the
 * PWM hardware at 0% and has to restart it afterwards, and that once seemed
 * to leave the screen dark. Instead the PWM keeps running with its shortest
 * pulse (DARK_PULSE_NS, ~0.025% of the 1 ms period), which looks black.
 *
 * When the backlight goes dark the panel itself is switched off too
 * (display_blanking_on, the ST7789's "display off" command), and back on
 * before the backlight returns. Both are done on ZMK's display work queue so
 * the commands can't land in the middle of a screen redraw on the SPI bus.
 *
 * Only built with a fixed brightness (no ambient light sensor), since the
 * Prospector module's sensor thread would otherwise keep overriding it.
 * Settings: Kconfig at the repo root, values in config/<keyboard>_dongle.conf.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/display.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// the backlight is the Prospector shield's pwm-leds child `disp_bl`
static const struct device *backlight = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
#define BACKLIGHT_IDX DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

// the same backlight as a raw PWM channel, for the "0%" pulse below
static const struct pwm_dt_spec backlight_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(disp_bl));

// 4 cycles of the 16 MHz PWM clock: must stay above 0 cycles, or the driver
// treats it as 0% and stops the PWM
#define DARK_PULSE_NS 250

static void set_backlight(uint8_t percent) {
    if (!device_is_ready(backlight)) {
        return;
    }
    int ret = percent == 0 ? pwm_set_pulse_dt(&backlight_pwm, PWM_NSEC(DARK_PULSE_NS))
                           : led_set_brightness(backlight, BACKLIGHT_IDX, percent);
    if (ret < 0) {
        LOG_WRN("Could not set backlight to %d%% (%d)", percent, ret);
    }
}

static const struct device *panel = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

// brightness the backlight should go to next; applied on the display queue
static atomic_t target_percent;
static bool panel_off;

static void apply_backlight_cb(struct k_work *work) {
    uint8_t percent = (uint8_t)atomic_get(&target_percent);

    if (percent > 0 && panel_off) {
        display_blanking_off(panel);
        panel_off = false;
    }
    set_backlight(percent);
    if (percent == 0 && !panel_off && device_is_ready(panel)) {
        display_blanking_on(panel);
        panel_off = true;
    }
}

static K_WORK_DEFINE(apply_backlight_work, apply_backlight_cb);

static void request_backlight(uint8_t percent) {
    atomic_set(&target_percent, percent);
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &apply_backlight_work);
    } else {
        set_backlight(percent); // no screen yet, so no panel to switch
    }
}

static void backlight_off_cb(struct k_work *work) { request_backlight(0); }

static K_WORK_DELAYABLE_DEFINE(backlight_off_work, backlight_off_cb);

static int idle_backlight_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        k_work_cancel_delayable(&backlight_off_work);
        request_backlight(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
        break;
    case ZMK_ACTIVITY_IDLE:
        request_backlight(CONFIG_WATERFOWL_DONGLE_IDLE_BRIGHTNESS);
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
