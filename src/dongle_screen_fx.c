/*
 * Prospector dongle: full-screen animations that react to typing, as extra
 * "screens" next to the Prospector module's own (src/screen_fx/fx.c draws
 * them; the &scr_next key steps through them, see config/keymap.dtsi).
 *
 * Screen 0 is the module's status screen, untouched. For the others LVGL is
 * shown an empty screen, so it has nothing to redraw, and the frames are
 * written straight to the panel from here. Everything runs on ZMK's display
 * work queue, the same thread LVGL draws from, so the two never talk to the
 * panel at once.
 *
 * A frame is 140x120 palette indexes, sent doubled (280x240) in bands of 16
 * panel rows. Only the part of a band that changed since the last frame is
 * sent: the SPI link is what limits the frame rate (a whole frame takes
 * about 70 ms at 16 MHz), not working the picture out.
 *
 * With the panel's touch wires connected (dts/prospector_touch.dtsi), a swipe
 * across the screen also steps through the screens, and the animations react
 * to a finger.
 *
 * Settings: Kconfig at the repo root.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/split/central.h>
#include <zmk/usb.h>

#include "dongle_screen_fx.h"
#include "screen_fx/fx.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PANEL_W (FX_W * 2)
#define PANEL_H (FX_H * 2)
#define BAND_ROWS 8 /* frame rows sent per write */
#define FRAME_MS 33
#define STATUS_MS 500
#define BANNER_MS 2000
/* after leaving the module's screen: time for LVGL to finish with the panel */
#define TAKEOVER_MS 300

static const struct device *panel = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static uint8_t frame[FX_W * FX_H];
static uint8_t shown[FX_W * FX_H]; /* what the panel has now */
static uint16_t palette[256];      /* RGB565 in the panel's byte order */
static uint16_t band[PANEL_W * BAND_ROWS * 2];

/* --- what the rest of the keyboard tells us (any thread) -------------------- */

static atomic_t wanted_screen; /* 0 = the module's, 1.. = animation */
static atomic_t paused;        /* keyboard idle: the panel is off */
static atomic_t wpm;
static atomic_t halves_connected; /* bit per half */

static atomic_t touch; /* TOUCH_DOWN | x << 8 | y, in frame pixels */
#define TOUCH_DOWN BIT(16)

static struct k_spinlock keys_lock;
static uint8_t keys[FX_MAX_KEYS];
static uint8_t n_keys;

/* --- display work queue only ------------------------------------------------- */

static int screen; /* the one showing */
static lv_obj_t *module_screen;
static lv_obj_t *empty_screen;
static bool redraw_all;
static float energy;
static uint32_t last_frame_at, status_at, banner_until;
static struct fx_status status;

static void frame_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame_cb);

static void read_status(void) {
    for (int half = 0; half < 2; half++) {
        uint8_t level = 0;
        if (half < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT) {
            zmk_split_central_get_peripheral_battery_level(half, &level);
        }
        status.battery[half] = level;
        status.half_connected[half] = (atomic_get(&halves_connected) & BIT(half)) != 0;
    }

    if (zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB) {
        strcpy(status.host, "USB");
        status.host_connected = zmk_usb_is_hid_ready();
    } else {
        int profile = zmk_ble_active_profile_index();
        status.host[0] = 'B';
        status.host[1] = 'T';
        status.host[2] = '1' + CLAMP(profile, 0, 8);
        status.host[3] = '\0';
        status.host_connected = zmk_ble_active_profile_is_connected();
    }
}

/* Send what changed in the frame (or all of it) to the panel. */
static void show_frame(void) {
    for (int top = 0; top < FX_H; top += BAND_ROWS) {
        int x0 = 0, x1 = FX_W - 1;

        if (!redraw_all) {
            /* the columns this band differs in */
            x0 = FX_W;
            x1 = -1;
            for (int y = top; y < top + BAND_ROWS; y++) {
                const uint8_t *now = &frame[y * FX_W], *was = &shown[y * FX_W];
                for (int x = 0; x < x0; x++) {
                    if (now[x] != was[x]) {
                        x0 = x;
                        break;
                    }
                }
                for (int x = FX_W - 1; x > x1; x--) {
                    if (now[x] != was[x]) {
                        x1 = x;
                        break;
                    }
                }
            }
            if (x1 < x0) {
                continue;
            }
        }

        const int width = (x1 - x0 + 1) * 2;
        uint16_t *out = band;
        for (int y = top; y < top + BAND_ROWS; y++) {
            const uint8_t *in = &frame[y * FX_W + x0];
            for (int x = 0; x < width; x += 2) {
                out[x] = out[x + 1] = palette[*in++];
            }
            memcpy(out + width, out, width * sizeof(band[0]));
            out += width * 2;
        }

        const struct display_buffer_descriptor desc = {
            .buf_size = width * BAND_ROWS * 2 * sizeof(band[0]),
            .width = width,
            .height = BAND_ROWS * 2,
            .pitch = width,
        };
        display_write(panel, x0 * 2, top * 2, &desc, band);
    }
    memcpy(shown, frame, sizeof(shown));
    redraw_all = false;
}

static void schedule_frame(uint32_t delay_ms) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &frame_work, K_MSEC(delay_ms));
}

static void frame_cb(struct k_work *work) {
    if (screen == 0 || atomic_get(&paused)) {
        return;
    }

    const uint32_t now = k_uptime_get_32();
    struct fx_input in = {.step = CLAMP(now - last_frame_at, 10, 200) / 33.3f};
    last_frame_at = now;

    K_SPINLOCK(&keys_lock) {
        in.n_keys = n_keys;
        memcpy(in.keys, keys, n_keys);
        n_keys = 0;
    }

    const atomic_val_t finger = atomic_get(&touch);
    in.touching = (finger & TOUCH_DOWN) != 0;
    in.touch_x = (finger >> 8) & 0xff;
    in.touch_y = finger & 0xff;

    /* energy: follows words per minute (quick up, slow down), and each key
     * nudges it straight away, since ZMK only updates the WPM once a second */
    const float target = MIN(1.0f, atomic_get(&wpm) / (float)CONFIG_DONGLE_SCREEN_FX_WPM_REFERENCE);
    const float rate = (target > energy ? 0.12f : 0.03f) * in.step;
    energy += (target - energy) * MIN(rate, 1.0f) + in.n_keys * 0.03f;
    energy = CLAMP(energy, 0.0f, 1.0f);
    in.energy = energy;

    if (now - status_at >= STATUS_MS) {
        status_at = now;
        read_status();
    }

    fx_frame(screen - 1, frame, &in);
    fx_draw_status(frame, &status, (int32_t)(banner_until - now) > 0 ? fx_name(screen - 1) : NULL);
    show_frame();

    /* always leave the queue a moment for LVGL's own tick and the backlight */
    const uint32_t took = k_uptime_get_32() - now;
    schedule_frame(took + 5 < FRAME_MS ? FRAME_MS - took : 5);
}

static void start_animation(uint32_t delay_ms) {
    uint16_t native[256];
    fx_start(screen - 1, native);
    for (int i = 0; i < 256; i++) {
        palette[i] = sys_cpu_to_be16(native[i]);
    }
    redraw_all = true;
    last_frame_at = k_uptime_get_32();
    status_at = last_frame_at - STATUS_MS;
    banner_until = last_frame_at + delay_ms + BANNER_MS;
    schedule_frame(delay_ms);
}

/* Switch to the wanted screen, or pick up again after the keyboard was idle. */
static void apply_cb(struct k_work *work) {
    const int want = atomic_get(&wanted_screen);

    if (want == screen) {
        if (screen != 0 && !atomic_get(&paused)) {
            redraw_all = true;
            last_frame_at = k_uptime_get_32();
            schedule_frame(0);
        }
        return;
    }

    uint32_t delay_ms = 0;
    if (screen == 0) {
        struct display_capabilities caps;
        display_get_capabilities(panel, &caps);
        if (caps.x_resolution != PANEL_W || caps.y_resolution != PANEL_H) {
            LOG_ERR("screen fx: expected a %dx%d panel, not %dx%d", PANEL_W, PANEL_H,
                    caps.x_resolution, caps.y_resolution);
            atomic_set(&wanted_screen, 0);
            return;
        }
        /* park LVGL on an empty screen: nothing on it ever needs redrawing */
        module_screen = lv_screen_active();
        if (empty_screen == NULL) {
            empty_screen = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(empty_screen, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(empty_screen, LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_screen_load(empty_screen);
        delay_ms = TAKEOVER_MS;
    }

    screen = want;
    if (screen == 0) {
        k_work_cancel_delayable(&frame_work);
        lv_screen_load(module_screen);
        lv_obj_invalidate(module_screen);
        return;
    }
    start_animation(delay_ms);
}

static K_WORK_DEFINE(apply_work, apply_cb);

static void request_apply(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &apply_work);
    }
}

void dongle_screen_fx_step(int delta) {
    if (!zmk_display_is_initialized()) {
        return;
    }
    const int screens = fx_count() + 1;
    atomic_set(&wanted_screen, (atomic_get(&wanted_screen) + screens + delta % screens) % screens);
    request_apply();
}

void dongle_screen_fx_next(void) { dongle_screen_fx_step(1); }

/* --- touch ---------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_FX_TOUCH)

#define SWIPE_MIN 60 /* panel pixels sideways for a swipe */

/* The touch controller reports the panel's own upright 240x280; the picture
 * is on its side (the module turns it 270 degrees, or 90 when flipped). */
static void touch_to_panel(int raw_x, int raw_y, int *x, int *y) {
    *x = raw_y;
    *y = PANEL_H - 1 - raw_x;
    if (IS_ENABLED(CONFIG_PROSPECTOR_ROTATE_DISPLAY_180) !=
        IS_ENABLED(CONFIG_DONGLE_SCREEN_FX_TOUCH_UPSIDE_DOWN)) {
        *x = PANEL_W - 1 - *x;
        *y = PANEL_H - 1 - *y;
    }
    *x = CLAMP(*x, 0, PANEL_W - 1);
    *y = CLAMP(*y, 0, PANEL_H - 1);
}

static void touch_cb(struct input_event *evt, void *user_data) {
    static int raw_x, raw_y, start_x, start_y;
    static bool down;

    if (evt->type == INPUT_EV_ABS && evt->code == INPUT_ABS_X) {
        raw_x = evt->value;
    } else if (evt->type == INPUT_EV_ABS && evt->code == INPUT_ABS_Y) {
        raw_y = evt->value;
    } else if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH) {
        int x, y;
        touch_to_panel(raw_x, raw_y, &x, &y);

        if (evt->value) {
            if (!down) {
                start_x = x;
                start_y = y;
            }
            down = true;
            atomic_set(&touch, TOUCH_DOWN | ((x / 2) << 8) | (y / 2));
        } else if (down) {
            down = false;
            atomic_set(&touch, 0);
            /* a swipe: mostly sideways. Leftwards is "next", like a page. */
            const int dx = x - start_x, dy = y - start_y;
            if (abs(dx) >= SWIPE_MIN && abs(dx) > 2 * abs(dy)) {
                dongle_screen_fx_step(dx < 0 ? 1 : -1);
            }
        }
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(prospector_touch)), touch_cb, NULL);

#endif

/* --- events ------------------------------------------------------------------- */

static int screen_fx_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        if (pos->state) {
            K_SPINLOCK(&keys_lock) {
                if (n_keys < FX_MAX_KEYS) {
                    keys[n_keys++] = (uint8_t)pos->position;
                }
            }
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_wpm_state_changed *wpm_ev = as_zmk_wpm_state_changed(eh);
    if (wpm_ev != NULL) {
        atomic_set(&wpm, CLAMP(wpm_ev->state, 0, 300));
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_split_central_status_changed *half = as_zmk_split_central_status_changed(eh);
    if (half != NULL) {
        if (half->slot < 2) {
            if (half->connected) {
                atomic_set_bit(&halves_connected, half->slot);
            } else {
                atomic_clear_bit(&halves_connected, half->slot);
            }
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);
    if (activity != NULL) {
        /* the panel is switched off while idle (src/dongle_idle_backlight.c) */
        const bool idle = activity->state != ZMK_ACTIVITY_ACTIVE;
        if (atomic_set(&paused, idle) && !idle) {
            request_apply();
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_screen_fx, screen_fx_listener);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_position_state_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_wpm_state_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_activity_state_changed);

/* --- the screen to start on ----------------------------------------------------- */

#if CONFIG_DONGLE_SCREEN_FX_START > 0

static void start_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(start_work, start_cb);

static void start_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        k_work_schedule(&start_work, K_MSEC(500));
        return;
    }
    atomic_set(&wanted_screen, MIN(CONFIG_DONGLE_SCREEN_FX_START, fx_count()));
    request_apply();
}

static int screen_fx_init(void) {
    /* after the module's screen has drawn itself once */
    k_work_schedule(&start_work, K_MSEC(1500));
    return 0;
}

SYS_INIT(screen_fx_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
