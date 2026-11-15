/*
 * Prospector dongle: dim the screen backlight when the keyboard goes idle,
 * switch it off a while later, and restore it on the next key press.
 *
 * It fades rather than jumps: up over UP_MS, down over DOWN_MS, evenly to
 * the eye (the pulse goes by the square of that). With the animated screens
 * (src/dongle_screen_fx.c) it waits FX_POWER_OFF_MS before going down to
 * nothing, while they close the picture, and at power-up it stays dark until
 * they have their first picture (dongle_backlight_on), so the Prospector
 * module's own screen isn't seen first.
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
 * Settings: Kconfig at the repo root, values in config/prospector_adapter.conf.
 */

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/display.h>

#include "dongle_backlight.h"
#include "screen_fx/fx.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// the backlight is the Prospector shield's pwm-leds child `disp_bl`
static const struct device *backlight = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
#define BACKLIGHT_IDX DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

// the same backlight as a raw PWM channel, for the "0%" pulse and the fades
static const struct pwm_dt_spec backlight_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(disp_bl));

// 4 cycles of the 16 MHz PWM clock: must stay above 0 cycles, or the driver
// treats it as 0% and stops the PWM
#define DARK_PULSE_NS 250

#define UP_MS 1000
#define DOWN_MS 400
#define STEP_MS 20
// going down to nothing waits for the animated screens to close the picture
#define CLOSING_MS (IS_ENABLED(CONFIG_DONGLE_SCREEN_FX) ? FX_POWER_OFF_MS : 0)

// Where a fade has got to, in thousandths of full: a pulse of that much of
// the period, as led_set_brightness gives for a percent.
static void set_level(int thousandths) {
    if (!device_is_ready(backlight_pwm.dev)) {
        return;
    }
    const uint32_t pulse = (uint32_t)((uint64_t)backlight_pwm.period * thousandths / 1000);
    int ret = pwm_set_pulse_dt(&backlight_pwm, MAX(pulse, PWM_NSEC(DARK_PULSE_NS)));
    if (ret < 0) {
        LOG_WRN("Could not set backlight to %d/1000 (%d)", thousandths, ret);
    }
}

// Where it ends up.
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

// what was last asked for (any thread); `asked` counts the requests
static atomic_t target_percent, target_over_ms, asked;

// the fade under way: display queue only, but for level_now before there is a screen
static int level_now = CONFIG_PROSPECTOR_FIXED_BRIGHTNESS * 10; // thousandths; the module sets this at power-up
static int level_from, level_to;
static uint32_t fade_began, fade_ms;
static atomic_val_t answered;
static bool panel_off;

static void fade_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(fade_work, fade_cb);

static void fade_cb(struct k_work *work) {
    const uint32_t now = k_uptime_get_32();
    const atomic_val_t request = atomic_get(&asked);

    if (request != answered) {
        answered = request;
        level_from = level_now;
        level_to = (int)atomic_get(&target_percent) * 10;
        fade_began = now;
        fade_ms = (uint32_t)atomic_get(&target_over_ms);
        if (level_to > 0 && panel_off) {
            display_blanking_off(panel);
            panel_off = false;
        }
    }

    const float k = fade_ms > 0 ? MIN((float)(now - fade_began) / fade_ms, 1.0f) : 1.0f;
    if (k < 1.0f) {
        // even steps of what the eye sees, which goes by about the root of the light
        const float from = sqrtf(level_from), to = sqrtf(level_to), at = from + (to - from) * k;
        level_now = (int)(at * at);
        set_level(level_now);
        k_work_reschedule_for_queue(zmk_display_work_q(), &fade_work, K_MSEC(STEP_MS));
        return;
    }
    level_now = level_to;
    set_backlight((uint8_t)(level_to / 10));
    if (level_to == 0 && !panel_off && device_is_ready(panel)) {
        display_blanking_on(panel);
        panel_off = true;
    }
}

// Fade to `percent` over `over_ms`, starting `after_ms` from now.
static void request_backlight(uint8_t percent, uint32_t over_ms, uint32_t after_ms) {
    atomic_set(&target_percent, percent);
    atomic_set(&target_over_ms, (atomic_val_t)over_ms);
    atomic_inc(&asked);
    if (zmk_display_is_initialized()) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &fade_work, K_MSEC(after_ms));
    } else {
        // no screen yet, so no panel to switch, and nothing to see fade
        set_backlight(percent);
        level_now = percent * 10;
    }
}

static void backlight_off_cb(struct k_work *work) { request_backlight(0, DOWN_MS, 0); }

static K_WORK_DELAYABLE_DEFINE(backlight_off_work, backlight_off_cb);

static int idle_backlight_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        k_work_cancel_delayable(&backlight_off_work);
        request_backlight(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS, UP_MS, 0);
        break;
    case ZMK_ACTIVITY_IDLE:
        request_backlight(CONFIG_WATERFOWL_DONGLE_IDLE_BRIGHTNESS, DOWN_MS,
                          CONFIG_WATERFOWL_DONGLE_IDLE_BRIGHTNESS == 0 ? CLOSING_MS : 0);
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

/*
 * Power-up. With the animated screens taking the place of the module's own,
 * the backlight stays dark until their first picture is ready, which then
 * comes up with it. This runs after the module has set its fixed brightness
 * (both at APPLICATION level, that one at the default priority). Should no
 * picture come, the backlight comes up by itself.
 */
#define LIGHT_ANYWAY_MS 5000

static atomic_t lit;

void dongle_backlight_on(void) {
    if (!atomic_set(&lit, 1)) {
        request_backlight(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS, UP_MS, 0);
    }
}

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_FX) && !IS_ENABLED(CONFIG_DONGLE_SCREEN_FX_MODULE_SCREEN)

static void light_anyway_cb(struct k_work *work) { dongle_backlight_on(); }

static K_WORK_DELAYABLE_DEFINE(light_anyway_work, light_anyway_cb);

static int dark_at_start(void) {
    set_backlight(0);
    level_now = 0;
    k_work_schedule(&light_anyway_work, K_MSEC(LIGHT_ANYWAY_MS));
    return 0;
}

SYS_INIT(dark_at_start, APPLICATION, 99);

#endif
