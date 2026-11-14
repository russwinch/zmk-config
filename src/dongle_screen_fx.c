/*
 * Prospector dongle: full-screen animations that react to typing, as extra
 * "screens" next to the Prospector module's own (src/screen_fx/fx.c draws
 * them; the &scr_next and &scr_prev keys step through them, see config/keymap.dtsi).
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
 * The pictures need about 60 KB, which the dongle doesn't have free. LVGL's
 * drawing buffers are bigger than that and sit unused while it is parked on
 * the empty screen, so the animation's memory is borrowed from one of them
 * (borrow_memory). Nothing has to be handed back: LVGL simply draws over it
 * when its own screen returns.
 *
 * The batteries and the output are drawn over the picture as the module's
 * Field screen draws them, when that is the layout in use and so its fonts
 * are in the firmware (src/screen_fx/status_overlay.c). Otherwise, and if
 * the borrowed buffer has no room for it, in the animations' own small
 * letters.
 *
 * The screen chosen with the keys is kept in ZMK's settings, by name, and
 * shown again after a restart (see "the chosen screen" below).
 *
 * Settings: Kconfig at the repo root.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <lvgl.h>

#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/split/central.h>
#include <zmk/usb.h>

#include "dongle_screen_fx.h"
#include "screen_fx/fx.h"

/* The status text in the Field screen's style needs that screen's fonts. */
#define FIELD_STATUS IS_ENABLED(CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD)
#if FIELD_STATUS
#include "screen_fx/status_overlay.h"
#endif

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

#define PIXELS (FX_W * FX_H)
#define BAND_PIXELS (PANEL_W * BAND_ROWS * 2)

/* borrowed from LVGL while an animation shows: see borrow_memory */
static uint8_t *frame;
static uint8_t *shown; /* what the panel has now */
static uint16_t *band;

static uint16_t palette[256]; /* RGB565 in the panel's byte order */

/* --- what the rest of the keyboard tells us (any thread) -------------------- */

static atomic_t wanted_screen; /* 0 = the module's, 1.. = animation */
static atomic_t chosen;        /* a key has picked the screen since power-up */
static atomic_t paused;        /* keyboard idle: the panel is off */
static atomic_t wpm;
static atomic_t halves_connected; /* bit per half */

static struct k_spinlock keys_lock;
static uint8_t keys[FX_MAX_KEYS];
static uint8_t n_keys;
static char chars[FX_MAX_CHARS]; /* what the keys typed, for the terminal */
static uint8_t n_chars;

/* --- display work queue only ------------------------------------------------- */

static int screen; /* the one showing */
static lv_obj_t *module_screen;
static lv_obj_t *empty_screen;
static bool start_pending; /* the animation has still to be set up */
static bool redraw_all;
static float energy;
static uint32_t last_frame_at, status_at, banner_until;
static uint32_t status_never; /* status_at before the first reading */
static struct fx_status status;
static bool overlay; /* the Field-style status text is in use */
static bool own_status; /* this animation draws the status itself */

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

    status.usb = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    status.profile = CLAMP(zmk_ble_active_profile_index(), 0, 8);
    status.host_open = zmk_ble_active_profile_is_open();
    if (status.usb) {
        strcpy(status.host, "USB");
        status.host_connected = zmk_usb_is_hid_ready();
    } else {
        int profile = status.profile;
        status.host[0] = 'B';
        status.host[1] = 'T';
        status.host[2] = '1' + profile;
        status.host[3] = '\0';
        status.host_connected = zmk_ble_active_profile_is_connected();
    }
}

#if FIELD_STATUS
static bool same_status(const struct fx_status *a, const struct fx_status *b) {
    return a->battery[0] == b->battery[0] && a->battery[1] == b->battery[1] &&
           a->half_connected[0] == b->half_connected[0] && a->half_connected[1] == b->half_connected[1] &&
           a->host_connected == b->host_connected && a->usb == b->usb && a->host_open == b->host_open &&
           a->profile == b->profile;
}
#endif

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
#if FIELD_STATUS
        const bool text = overlay && !own_status && top <= OVERLAY_FRAME_ROW1 && top + BAND_ROWS > OVERLAY_FRAME_ROW0;
#endif

        const int width = (x1 - x0 + 1) * 2;
        uint16_t *out = band;
        for (int y = top; y < top + BAND_ROWS; y++) {
            const uint8_t *in = &frame[y * FX_W + x0];
            for (int x = 0; x < width; x += 2) {
                out[x] = out[x + 1] = palette[*in++];
            }
            memcpy(out + width, out, width * sizeof(*band));
#if FIELD_STATUS
            if (text) {
                overlay_blend_row(out, x0 * 2, y * 2, width);
                overlay_blend_row(out + width, x0 * 2, y * 2 + 1, width);
            }
#endif
            out += width * 2;
        }

        const struct display_buffer_descriptor desc = {
            .buf_size = width * BAND_ROWS * 2 * sizeof(*band),
            .width = width,
            .height = BAND_ROWS * 2,
            .pitch = width,
        };
        display_write(panel, x0 * 2, top * 2, &desc, band);
    }
    memcpy(shown, frame, PIXELS);
    redraw_all = false;
}

static void schedule_frame(uint32_t delay_ms) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &frame_work, K_MSEC(delay_ms));
}

/*
 * Lay the animation's pictures out in the LVGL drawing buffer that is free.
 * Only once LVGL has finished drawing and sending the empty screen (the
 * TAKEOVER_MS wait): after that it has no reason to touch either buffer.
 */
static bool borrow_memory(void) {
    lv_display_t *display = lv_display_get_default();
    lv_draw_buf_t *buf = display != NULL ? lv_display_get_buf_active(display) : NULL;
    const size_t needed = 3 * PIXELS + BAND_PIXELS * sizeof(*band);

    if (buf == NULL || buf->data == NULL || buf->data_size < needed) {
        LOG_ERR("screen fx: no LVGL buffer of %u bytes to borrow", (unsigned int)needed);
        return false;
    }
    frame = buf->data;
    shown = frame + PIXELS;
    fx_set_canvas(shown + PIXELS);
    band = (uint16_t *)(shown + 2 * PIXELS); /* 3 * PIXELS is a multiple of 4 */
#if FIELD_STATUS
    overlay = buf->data_size >= needed + OVERLAY_BYTES;
    if (overlay) {
        overlay_set_memory(buf->data + needed);
    } else {
        LOG_WRN("screen fx: no room for the Field status text");
    }
#endif
    return true;
}

static void begin_animation(void) {
    uint16_t native[256];
    fx_start(screen - 1, native);
    own_status = fx_draws_status(screen - 1);
    for (int i = 0; i < 256; i++) {
        palette[i] = sys_cpu_to_be16(native[i]);
    }
    /* nothing typed before this screen came up belongs on it */
    K_SPINLOCK(&keys_lock) {
        n_keys = n_chars = 0;
    }
    redraw_all = true;
    last_frame_at = k_uptime_get_32();
    status_at = status_never = last_frame_at - STATUS_MS;
    banner_until = last_frame_at + BANNER_MS;
}

static void apply_cb(struct k_work *work);
static K_WORK_DEFINE(apply_work, apply_cb);

static void frame_cb(struct k_work *work) {
    if (screen == 0 || atomic_get(&paused)) {
        return;
    }

    if (start_pending) {
        if (!borrow_memory()) {
            /* back to the module's screen */
            atomic_set(&wanted_screen, 0);
            k_work_submit_to_queue(zmk_display_work_q(), &apply_work);
            return;
        }
        begin_animation();
        start_pending = false;
    }

    const uint32_t now = k_uptime_get_32();
    struct fx_input in = {.step = CLAMP(now - last_frame_at, 10, 200) / 33.3f};
    last_frame_at = now;

    K_SPINLOCK(&keys_lock) {
        in.n_keys = n_keys;
        memcpy(in.keys, keys, n_keys);
        n_keys = 0;
        in.n_chars = n_chars;
        memcpy(in.chars, chars, n_chars);
        n_chars = 0;
    }

    /* energy: follows words per minute (quick up, slow down), and each key
     * nudges it straight away, since ZMK only updates the WPM once a second */
    const float target = MIN(1.0f, atomic_get(&wpm) / (float)CONFIG_DONGLE_SCREEN_FX_WPM_REFERENCE);
    const float rate = (target > energy ? 0.12f : 0.03f) * in.step;
    energy += (target - energy) * MIN(rate, 1.0f) + in.n_keys * 0.03f;
    energy = CLAMP(energy, 0.0f, 1.0f);
    in.energy = energy;

    if (now - status_at >= STATUS_MS) {
#if FIELD_STATUS
        const struct fx_status was = status;
        const bool first = status_at == status_never;
#endif
        status_at = now;
        read_status();
#if FIELD_STATUS
        /* the text is only worked out again when it changes */
        if (overlay && (first || !same_status(&was, &status))) {
            overlay_render(&status);
            redraw_all = true;
        }
#endif
    }

    in.status = &status;
    fx_frame(screen - 1, frame, &in);
    fx_draw_status(frame, overlay || own_status ? NULL : &status,
                   (int32_t)(banner_until - now) > 0 ? fx_name(screen - 1) : NULL);
    show_frame();

    /* always leave the queue a moment for LVGL's own tick and the backlight */
    const uint32_t took = k_uptime_get_32() - now;
    schedule_frame(took + 5 < FRAME_MS ? FRAME_MS - took : 5);
}

/* The animation itself is set up by the first frame, once LVGL is done. */
static void start_animation(uint32_t delay_ms) {
    start_pending = true;
    schedule_frame(delay_ms);
}

/* Switch to the wanted screen, or pick up again after the keyboard was idle. */
static void apply_cb(struct k_work *work) {
    const int want = atomic_get(&wanted_screen);

    if (want == screen) {
        if (screen != 0 && !atomic_get(&paused)) {
            /* the panel was off: send the whole picture again */
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

static void request_apply(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &apply_work);
    }
}

/* --- the chosen screen, kept over a restart ----------------------------------- */

/*
 * Saved by name, so that adding or dropping an animation doesn't turn the
 * saved one into its neighbour. Only once the screen has been left showing
 * for SAVE_DELAY_MS: stepping through them all writes to flash once.
 */
#define STATUS_NAME "STATUS" /* the module's screen */

static const char *screen_name(int which) { return which == 0 ? STATUS_NAME : fx_name(which - 1); }

#if IS_ENABLED(CONFIG_SETTINGS)

#define SAVED_SETTING "screen_fx/screen"

static char saved_name[16]; /* what flash holds; empty: nothing saved */

static int saved_screen_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (!settings_name_steq(key, "screen", NULL)) {
        return -ENOENT;
    }
    if (len >= sizeof(saved_name)) {
        return -EINVAL;
    }
    const ssize_t got = read_cb(cb_arg, saved_name, len);
    if (got < 0) {
        saved_name[0] = '\0';
        return (int)got;
    }
    saved_name[got] = '\0';
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(dongle_screen_fx, "screen_fx", NULL, saved_screen_set, NULL, NULL);

static void save_cb(struct k_work *work) {
    const char *name = screen_name(atomic_get(&wanted_screen));

    if (strcmp(name, saved_name) == 0 || strlen(name) >= sizeof(saved_name)) {
        return;
    }
    const int err = settings_save_one(SAVED_SETTING, name, strlen(name));
    if (err != 0) {
        LOG_ERR("screen fx: saving the screen failed (%d)", err);
        return;
    }
    strcpy(saved_name, name);
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_cb);

#endif

/* The screen to start on: the saved one if it still exists, or -1. */
static int saved_screen(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    for (int which = 0; which <= fx_count(); which++) {
        if (strcmp(saved_name, screen_name(which)) == 0) {
            return which;
        }
    }
#endif
    return -1;
}

void dongle_screen_fx_step(int by) {
    if (!zmk_display_is_initialized()) {
        return;
    }
    const int screens = fx_count() + 1;
    atomic_set(&chosen, 1);
    atomic_set(&wanted_screen, (atomic_get(&wanted_screen) + (by < 0 ? screens - 1 : 1)) % screens);
    request_apply();
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_DONGLE_SCREEN_FX_SAVE_DELAY_MS));
#endif
}

/* --- events ------------------------------------------------------------------- */

/*
 * The character a key types, on a US layout (what the keycodes are named
 * for; a computer set to another layout may print something else for the
 * symbols). 0 for anything that isn't text, and for shortcuts.
 */
static char typed_char(const struct zmk_keycode_state_changed *ev) {
    static const char plain[] = "\n\0\b\t -=[]\\#;'`,./";
    static const char shifted[] = "\n\0\b\t _+{}|~:\"~<>?";
    const uint8_t mods = ev->implicit_modifiers | ev->explicit_modifiers | zmk_hid_get_explicit_mods();
    const bool shift = (mods & (MOD_LSFT | MOD_RSFT)) != 0;
    const uint32_t key = ev->keycode;

    if (ev->usage_page != HID_USAGE_KEY || (mods & ~(MOD_LSFT | MOD_RSFT)) != 0) {
        return 0;
    }
    if (key >= HID_USAGE_KEY_KEYBOARD_A && key <= HID_USAGE_KEY_KEYBOARD_Z) {
        return (shift ? 'A' : 'a') + (key - HID_USAGE_KEY_KEYBOARD_A);
    }
    if (key >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && key <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        const int digit = key - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        return shift ? "!@#$%^&*()"[digit] : "1234567890"[digit];
    }
    if (key >= HID_USAGE_KEY_KEYBOARD_RETURN_ENTER && key <= HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK) {
        return (shift ? shifted : plain)[key - HID_USAGE_KEY_KEYBOARD_RETURN_ENTER];
    }
    return 0;
}

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

    const struct zmk_keycode_state_changed *code = as_zmk_keycode_state_changed(eh);
    if (code != NULL) {
        /* only kept while an animation shows, and only until its next frame */
        const char c = code->state && atomic_get(&wanted_screen) != 0 ? typed_char(code) : 0;
        if (c != 0) {
            K_SPINLOCK(&keys_lock) {
                if (n_chars < FX_MAX_CHARS) {
                    chars[n_chars++] = c;
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
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_wpm_state_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_activity_state_changed);

/* --- the screen to start on ----------------------------------------------------- */

static void start_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(start_work, start_cb);

static void start_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        k_work_schedule(&start_work, K_MSEC(500));
        return;
    }
    if (atomic_get(&chosen)) {
        return; /* a key got there first */
    }
    const int saved = saved_screen();
    atomic_set(&wanted_screen, saved >= 0 ? saved : MIN(CONFIG_DONGLE_SCREEN_FX_START, fx_count()));
    request_apply();
}

static int screen_fx_init(void) {
    /* after the module's screen has drawn itself once, and ZMK has loaded
     * the settings (the saved screen among them) */
    k_work_schedule(&start_work, K_MSEC(1500));
    return 0;
}

SYS_INIT(screen_fx_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
