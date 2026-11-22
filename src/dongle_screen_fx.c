/*
 * Prospector dongle: full-screen animations that react to typing, as extra
 * "screens" next to the Prospector module's own (src/screen_fx/fx.c draws
 * them; the &scr_next and &scr_prev keys step through them, see config/keymap.dtsi).
 *
 * Screen 0 is the module's status screen, untouched; the keys only step to
 * it with DONGLE_SCREEN_FX_MODULE_SCREEN. For the others LVGL is
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
 * The batteries and the output are drawn over the picture in the fonts of
 * the module's Field screen (src/screen_fx/status_overlay.c), in colours
 * that suit each animation. If the borrowed buffer has no room for that
 * text, the animations run without it.
 *
 * The screen chosen with the keys is kept in ZMK's settings, by name, and
 * shown again after a restart (see "the chosen screen" below). So is the
 * colour theme, which the &scr_theme key steps through: the Keys, Status, Debug,
 * Tracker and Terminal screens are drawn in its colours (colors.h has the list), and its
 * name shows for a moment when it is turned to.
 *
 * Some of the animations are screensavers instead (fx_saver_way_in): the
 * keys skip them, and when no key has been pressed for a while
 * (DONGLE_SCREEN_FX_SAVER_MINUTES) one takes over, until a key brings the
 * chosen screen back: see "the screensaver" below.
 *
 * When the keyboard goes idle the panel is switched off
 * (src/dongle_idle_backlight.c): the picture first closes to a dot like an
 * old television's, and when the panel comes back on, and at power-up, the
 * picture comes through white noise: see "the panel going off and on" below.
 *
 * Settings: Kconfig at the repo root.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fatal.h>
#include <zephyr/linker/section_tags.h> /* __noinit */
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h> /* NRF_POWER->GPREGRET2 */
#include <zephyr/drivers/usb/usb_dc.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/adc/nrf-saadc-v2.h> /* NRF_SAADC_VDD */
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <lvgl.h>

#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/keymap.h>
#include <zmk/events/sensor_event.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/split/central.h>
#include <zmk/usb.h>

#include "dongle_backlight.h"
#include "dongle_wpm.h"
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
#include "dongle_half_info.h"
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include "dongle_usb_count.h"
#endif
#include "dongle_screen_fx.h"
#include "screen_fx/fx.h"

#include "screen_fx/status_overlay.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PANEL_W (FX_W * 2)
#define PANEL_H (FX_H * 2)
#define BAND_ROWS 8 /* frame rows sent per write */
#define FRAME_MS 33
#define DEBUG_FRAME_MS 100
#define STATUS_MS 500
#define NOTICE_MS 1500 /* a theme's name, or the brightness, stays up this long after it is turned to */
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
/* the same as fx.c gave it, and the one of the picture a transition has as
 * its still: see "the screensaver" */
static uint16_t native_palette[256], old_palette[256];

/* --- what the rest of the keyboard tells us (any thread) -------------------- */

static atomic_t wanted_screen; /* 0 = the module's, 1.. = animation */
static atomic_t wanted_theme;  /* colors.h's schemes, from 0 */
static atomic_t chosen;        /* a key has picked the screen since power-up */
static atomic_t paused;        /* keyboard idle: the panel is off */
static atomic_t sleep_wanted;  /* keyboard idle: the picture is to close, and then that */
/* the next picture comes through static: the first, and the one after the panel was off */
static atomic_t static_wanted = ATOMIC_INIT(1);
static atomic_t last_key_at;   /* uptime, ms */
static atomic_t saver_drop;    /* the panel went off with the screensaver showing */
static atomic_t wpm;
static atomic_t halves_connected; /* bit per half */

static struct k_spinlock keys_lock;
static uint8_t keys[FX_MAX_KEYS];
static uint8_t n_keys;
static char chars[FX_MAX_CHARS]; /* what the keys typed, for the terminal */
static uint8_t n_chars;

/*
 * For the Debug screen (fx_is_debug), kept up whichever screen shows, so
 * that it has the history when it is turned to: the last few events, the
 * keys held, and what has gone wrong with the Bluetooth links. The rest of
 * what it shows is read when it is drawn (read_debug).
 */
#define BOUNCE_KEYS 40
#define BOUNCE_WITHIN_MS 1000 /* a key back down sooner than this after going up is timed */

static struct k_spinlock debug_lock;
static struct fx_debug debug;
static uint32_t last_event_at;
static uint32_t released_at[BOUNCE_KEYS]; /* 0: not yet */
static const struct bt_conn *half_conn[2]; /* only ever compared, never used */

/*
 * What a held key is holding, for the Keys and Debug screens to colour it
 * by. Worked out from the keymap when the key goes down (key_pressed): a
 * modifier (&kp LSHIFT, or the hold side of a hold-tap such as a home row
 * mod) or a layer (&mo, &lt). The key only takes the colour while those
 * modifiers are really down or that layer is really on, so a hold-tap that
 * is tapped doesn't.
 */
enum { HOLDS_NOTHING, HOLDS_MODS, HOLDS_LAYER, FALLS_THROUGH /* &trans */ };
#define KEY_INFOS 64
static uint8_t key_holds[KEY_INFOS], key_holds_what[KEY_INFOS]; /* modifier bits, or a layer */

/* Each behavior that can hold something, by the name keymap bindings have it under. */
#define BEHAVIOR_KIND(node, kind) {DEVICE_DT_NAME(node), kind},
#define HOLD_OF(node) DT_PHANDLE_BY_IDX(node, bindings, 0)
#define HOLD_TAP_KIND(node)                                                                                  \
    BEHAVIOR_KIND(node, DT_NODE_HAS_COMPAT(HOLD_OF(node), zmk_behavior_momentary_layer) ? HOLDS_LAYER        \
                        : DT_NODE_HAS_COMPAT(HOLD_OF(node), zmk_behavior_key_press) ? HOLDS_MODS             \
                                                                                    : HOLDS_NOTHING)
static const struct {
    const char *name;
    uint8_t kind;
} behavior_kinds[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_hold_tap, HOLD_TAP_KIND)
    DT_FOREACH_STATUS_OKAY_VARGS(zmk_behavior_key_press, BEHAVIOR_KIND, HOLDS_MODS)
    DT_FOREACH_STATUS_OKAY_VARGS(zmk_behavior_momentary_layer, BEHAVIOR_KIND, HOLDS_LAYER)
    DT_FOREACH_STATUS_OKAY_VARGS(zmk_behavior_transparent, BEHAVIOR_KIND, FALLS_THROUGH)
};

/* --- display work queue only ------------------------------------------------- */

static int screen; /* the one showing */
static lv_obj_t *module_screen;
static lv_obj_t *empty_screen;
static bool start_pending; /* the animation has still to be set up */
static bool redraw_all;
static float energy;
static uint32_t last_frame_at, status_at;
static struct fx_status status;
static bool overlay;    /* there was room for the status text */
/* the frame columns in which that text has changed since it was last sent (none: text_x1 < text_x0) */
static int text_x0 = FX_W, text_x1 = -1;
static bool is_debug;        /* the Debug screen is the one showing */
static bool plain;           /* that or the Keys screen: the readings, and no status text over them */
static struct fx_debug debug_shown;
static uint32_t frames_since, fps_at;
/* the notice that is up, until notice_until: the theme's name or the brightness */
enum { NOTICE_NONE, NOTICE_THEME, NOTICE_BRIGHTNESS };
static int notice;
static uint32_t notice_until;

/*
 * The screensaver. `wanted_screen` stays the screen that was chosen;
 * `screen` is the screensaver's animation while this isn't SAVER_OFF.
 *
 *   SAVER_IN   its way in (fx_saver_way_in), over the still of what was showing
 *   SAVER_ON   just the animation
 *   SAVER_OUT  a key was pressed: it reacts to the typing, and glitches
 *   SAVER_CUT  the wanted screen is running again and cuts in over the
 *              screensaver's still
 *
 * Only one animation can run (they share their memory), so the other
 * picture in a transition is always a still: the last frame sent, which
 * `shown` holds anyway, with the palette it had (old_palette). While a
 * transition shows, every frame is sent whole and `shown` is left alone.
 * The animation that comes back starts afresh.
 *
 * Over the module's own screen there is no still to be had: the way in is
 * over black, and the way out ends with that screen simply coming back.
 */
enum { SAVER_OFF, SAVER_IN, SAVER_ON, SAVER_OUT, SAVER_CUT };
static int saver;
static bool saver_over_module; /* SAVER_IN: no still, it started from the module's screen */
static uint32_t saver_began_at; /* keys after this end it */
static uint32_t stage_at;       /* when this stage began */
static uint32_t stage_frames;

/*
 * The panel going off and on. The keyboard going idle switches the panel
 * off (src/dongle_idle_backlight.c, which waits FX_POWER_OFF_MS before it
 * does). In that time the picture closes to a line and a dot
 * (FX_TR_CRT_OFF), ending black: so black is what the panel still holds
 * when it is switched on again, and not the last picture. The next picture,
 * like the first after power-up, comes through static (FX_TR_STATIC) while
 * the backlight comes up. These go over whatever the screensaver is doing.
 */
enum { POWER_STEADY, POWER_GOING_OFF, POWER_COMING_ON };
static int power;
static uint32_t power_at;
static bool lit; /* the backlight has been asked up for the first picture */

/* Only if idle means off: a dimmed panel keeps its picture. */
#if defined(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT) && CONFIG_WATERFOWL_DONGLE_IDLE_BRIGHTNESS == 0
#define CLOSES_WHEN_IDLE 1
#else
#define CLOSES_WHEN_IDLE 0
#endif

static void frame_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame_cb);
static void saver_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(saver_work, saver_cb);

#define BOLT_FLASH_MS 1000 /* a charging half's bolt: on three fifths of this, off the rest */

static void read_status(void) {
    for (int half = 0; half < 2; half++) {
        uint8_t level = 0;
        if (half < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT) {
            zmk_split_central_get_peripheral_battery_level(half, &level);
        }
        status.battery[half] = level;
        status.half_connected[half] = (atomic_get(&halves_connected) & BIT(half)) != 0;
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
        status.charging[half] = dongle_half_charging(half);
        /* the bolt beside it flashes until the level shown reads 100 (the
         * estimate, if any, stops at 99) */
        bool estimate;
        const uint8_t estimated = dongle_half_charge_estimate(half, &estimate);
        const uint8_t shown = estimate ? estimated : level;
        status.bolt_off[half] = status.charging[half] && shown > 0 && shown < 100 &&
                                k_uptime_get_32() % BOLT_FLASH_MS >= BOLT_FLASH_MS * 3 / 5;
#endif
    }

    status.usb = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    status.profile = CLAMP(zmk_ble_active_profile_index(), 0, 8);
    status.host_open = zmk_ble_active_profile_is_open();
    status.profile_connected = zmk_ble_active_profile_is_connected();
    status.host_connected = status.usb ? zmk_usb_is_hid_ready() : status.profile_connected;
}

/* --- the Debug screen's readings ---------------------------------------------- */

#if IS_ENABLED(CONFIG_BT_CTLR_CONN_RSSI)
/* How strongly the dongle hears the other end of a link, from the radio:
 * 0, or why not, for the Debug screen (fx_link.rssi_err). */
static uint8_t read_rssi(uint16_t handle, int8_t *rssi) {
    struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(struct bt_hci_cp_read_rssi));
    struct net_buf *rsp = NULL;
    if (buf == NULL) {
        return FX_RSSI_ERR_BUFFER;
    }
    struct bt_hci_cp_read_rssi *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);
    const int err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
    if (err != 0) {
        return (uint8_t)CLAMP(-err, 1, 99); /* -EIO etc.: the radio refused it */
    }
    if (rsp == NULL || rsp->len < sizeof(struct bt_hci_rp_read_rssi)) {
        if (rsp != NULL) {
            net_buf_unref(rsp);
        }
        return FX_RSSI_ERR_BUFFER;
    }
    const struct bt_hci_rp_read_rssi *rp = (const void *)rsp->data;
    const uint8_t status = rp->status;
    *rssi = rp->rssi;
    net_buf_unref(rsp);
    return status;
}
#endif

struct link_reading {
    struct fx_link *link[3]; /* left, right, the computer */
    uint16_t handle[3];
    bool found[3];
};

static void read_link(struct bt_conn *conn, void *data) {
    struct link_reading *reading = data;
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    int which = -1;
    if (info.role == BT_CONN_ROLE_CENTRAL) {
        K_SPINLOCK(&debug_lock) {
            which = conn == half_conn[0] ? 0 : conn == half_conn[1] ? 1 : -1;
        }
    } else if (bt_addr_le_eq(info.le.dst, zmk_ble_active_profile_addr())) {
        which = 2;
    }
    if (which < 0) {
        return;
    }
    struct fx_link *link = reading->link[which];
    link->up = true;
    reading->found[which] = bt_hci_get_conn_handle(conn, &reading->handle[which]) == 0;
}

static void read_keys(struct fx_debug *d);

/* The build, for the Debug screen: CI's run number (CMakeLists.txt), the
 * one in its release's name, build-149; DEV when built anywhere else. */
#ifdef DONGLE_BUILD_NUMBER
#define FIRMWARE_BUILD "B" STRINGIFY(DONGLE_BUILD_NUMBER)
#else
#define FIRMWARE_BUILD "DEV"
#endif

/* What lvgl_heap_stats fills in needs CONFIG_SYS_HEAP_RUNTIME_STATS. */
void lvgl_heap_stats(struct sys_memory_stats *stats);

static uint32_t reset_cause;

/*
 * A start that never shows a picture. More than once the dongle has come up
 * from a flash with the panel black and the backlight off, for good: the
 * display queue stuck, or the system halted after a crash (Zephyr halts on
 * a fatal error unless told otherwise). So: no first picture within
 * FIRST_PICTURE_BY_MS of power-up restarts the dongle, and so does a
 * crash, each noted in RAM that a restart keeps (boot_note) for the Debug
 * screen to show (HANG, or CRASH and Zephyr's reason). After such a
 * restart the dongle starts on the first animation rather than the saved
 * screen, in case that screen is what stops it (the saved one stays saved).
 * After HANG_RESTARTS_MAX of them in a row without a picture it stops
 * trying, and the backlight comes up anyway (src/dongle_idle_backlight.c).
 *
 * The note is also kept in GPREGRET2, a register a restart keeps, as
 * kind (2 bits), restarts in a row (2) and code (4, 15 for 15 and over):
 * the restart goes through the bootloader, which can overwrite the RAM, and
 * then the Debug screen said only SOFT. ZMK's boot mode uses GPREGRET.
 */
#define FIRST_PICTURE_BY_MS 15000
#define HANG_RESTARTS_MAX 3
#define BOOT_NOTE_MAGIC 0x5c3ee17aU

static __noinit struct {
    uint32_t magic;
    uint8_t crash, code; /* how this run ended, if it did: FX_CRASH_* */
    uint8_t troubled;    /* restarts in a row before a first picture */
} boot_note;
static uint8_t last_crash, last_crash_code; /* how the run before this one ended */

/*
 * Where a crash happened (the program counter), for the Debug screen to show
 * beside the reason, to look up in that build. Kept in the first KB of RAM,
 * which the dongle's Zephyr leaves out (config/prospector_adapter.overlay):
 * the Adafruit bootloader the restart goes through only uses RAM from
 * 0x20007F7C up (and the MBR the first 8 bytes), so this lasts through it
 * where boot_note in .noinit may not.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(crash_spot_ram))
#define CRASH_SPOT_MAGIC 0xc5a5f0a7U
struct crash_spot {
    uint32_t magic, pc;
};
#define CRASH_SPOT ((volatile struct crash_spot *)DT_REG_ADDR(DT_NODELABEL(crash_spot_ram)))
BUILD_ASSERT(sizeof(struct crash_spot) <= DT_REG_SIZE(DT_NODELABEL(crash_spot_ram)));
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(crash_spot_ram)) + DT_REG_SIZE(DT_NODELABEL(crash_spot_ram)) <=
                 CONFIG_SRAM_BASE_ADDRESS,
             "crash_spot_ram must be outside Zephyr's RAM");
#endif
static uint32_t last_crash_pc; /* where the run before this one crashed, 0 not known */
static bool safe_start;                     /* that run never showed a picture */
static atomic_t pictured;                   /* this run has shown one */

static void boot_note_valid(void) {
    if (boot_note.magic != BOOT_NOTE_MAGIC) { /* power-on, or the bootloader used the RAM */
        memset(&boot_note, 0, sizeof(boot_note));
        boot_note.magic = BOOT_NOTE_MAGIC;
        /* what the register kept, if anything */
        const uint8_t kept = (uint8_t)NRF_POWER->GPREGRET2;
        boot_note.crash = kept >> 6;
        boot_note.troubled = (kept >> 4) & 3;
        boot_note.code = kept & 0xf;
    }
}

/* The note into the register too, before a restart. */
static void boot_note_keep(void) {
    NRF_POWER->GPREGRET2 = (uint32_t)((boot_note.crash & 3) << 6 | MIN(boot_note.troubled, 3) << 4 |
                                      MIN(boot_note.code, 0xf));
}

static void boot_note_take(void) {
    boot_note_valid();
    last_crash = boot_note.crash;
    last_crash_code = boot_note.code;
    boot_note.crash = boot_note.code = 0;
#ifdef CRASH_SPOT
    if (last_crash == FX_CRASH_FAULT && CRASH_SPOT->magic == CRASH_SPOT_MAGIC) {
        last_crash_pc = CRASH_SPOT->pc;
    }
    CRASH_SPOT->magic = 0;
#endif
    safe_start = boot_note.troubled > 0;
    boot_note_keep(); /* just the restarts in a row, now */
}

/* The first picture is out (or the module's screen is showing instead). */
static void first_picture(void) {
    if (!atomic_set(&pictured, 1)) {
        boot_note.troubled = 0;
        boot_note_keep();
    }
}

static void picture_watch_cb(struct k_work *work) {
    if (atomic_get(&pictured) || boot_note.troubled >= HANG_RESTARTS_MAX) {
        return;
    }
    boot_note.crash = FX_CRASH_HANG;
    boot_note.code = FX_HANG_AT_START;
    boot_note.troubled++;
    boot_note_keep();
    sys_reboot(SYS_REBOOT_WARM);
}

static K_WORK_DELAYABLE_DEFINE(picture_watch_work, picture_watch_cb);

/*
 * The same after waking: the panel has been off while the keyboard was idle,
 * and the first key after brings it back, its backlight fading up and the
 * frames starting again, all on the display queue. Once (after a lunch
 * break) it stayed black and dark. So if no frame has begun within
 * FIRST_PICTURE_BY_MS of waking, while still awake and on one of these
 * screens, the dongle restarts (HANG 2 on the Debug screen), and starts
 * on the first animation once.
 */
static atomic_t frames_begun; /* frame_cb runs that got past the panel being off */
static atomic_t frames_at_wake;

static void wake_watch_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wake_watch_work, wake_watch_cb);

/* In place of Zephyr's, which halts: note it and restart. Any context. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf) {
    boot_note_valid();
#ifdef CRASH_SPOT
    CRASH_SPOT->pc = esf != NULL ? esf->basic.pc : 0;
    CRASH_SPOT->magic = CRASH_SPOT_MAGIC;
#else
    ARG_UNUSED(esf);
#endif
    boot_note.crash = FX_CRASH_FAULT;
    boot_note.code = (uint8_t)MIN(reason, 0xff);
    if (!atomic_get(&pictured)) {
        boot_note.troubled++;
    }
    boot_note_keep();
    if (atomic_get(&pictured) || boot_note.troubled <= HANG_RESTARTS_MAX) {
        sys_reboot(SYS_REBOOT_WARM);
    }
    k_fatal_halt(reason);
}

/* The dongle's own supply, for the Debug screen: the XIAO's 3.3 V regulator,
 * read by the SAADC on its VDD input (channel 6, which nothing else in this
 * firmware sets up). The USB 5 V isn't wired to anything the chip can
 * measure, only detected. */
#if IS_ENABLED(CONFIG_ADC) && DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
#define VDD_CHANNEL 6
static uint16_t read_vdd_mv(uint8_t *err) {
    static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc));
    static bool set_up;
    if (!device_is_ready(adc)) {
        *err = 1;
        return 0;
    }
    if (!set_up) {
        const struct adc_channel_cfg cfg = {
            .gain = ADC_GAIN_1_6,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10),
            .channel_id = VDD_CHANNEL,
            .input_positive = NRF_SAADC_VDD,
        };
        if (adc_channel_setup(adc, &cfg) != 0) {
            *err = 2;
            return 0;
        }
        set_up = true;
    }
    int16_t raw = 0;
    const struct adc_sequence seq = {
        .channels = BIT(VDD_CHANNEL),
        .buffer = &raw,
        .buffer_size = sizeof(raw),
        .resolution = 12,
    };
    if (adc_read(adc, &seq) != 0) {
        *err = 3;
        return 0;
    }
    int32_t mv = raw;
    if (adc_raw_to_millivolts(adc_ref_internal(adc), ADC_GAIN_1_6, 12, &mv) != 0 || mv <= 0) {
        *err = 4;
        return 0;
    }
    return (uint16_t)mv;
}
#else
static uint16_t read_vdd_mv(uint8_t *err) {
    *err = 9; /* not built in */
    return 0;
}
#endif

/* The dongle's own chip temperature, for the Debug screen. */
#if IS_ENABLED(CONFIG_SENSOR) && DT_NODE_HAS_STATUS(DT_NODELABEL(temp), okay)
static bool read_temp(int8_t *celsius, uint8_t *err) {
    static const struct device *const temp = DEVICE_DT_GET(DT_NODELABEL(temp));
    struct sensor_value v;
    *err = !device_is_ready(temp)                                    ? 1
           : sensor_sample_fetch(temp) != 0                          ? 2
           : sensor_channel_get(temp, SENSOR_CHAN_DIE_TEMP, &v) != 0 ? 3
                                                                     : 0;
    if (*err) {
        return false;
    }
    *celsius = (int8_t)CLAMP(v.val1, INT8_MIN, INT8_MAX);
    return true;
}
#else
static bool read_temp(int8_t *celsius, uint8_t *err) {
    *err = 9; /* not built in */
    return false;
}
#endif

/*
 * The dongle's own supply and temperature, for the Debug screen, each on a
 * thread of its own: the temperature read sometimes never comes back after
 * a flash (until the dongle is unplugged), and one that is stuck must blank
 * only itself, never the other or anything else. Every few seconds while
 * the readings are wanted.
 */
#define DONGLE_READINGS_EVERY_MS 5000
#define DONGLE_READINGS_STACK 1536
#define DONGLE_READ_STUCK_MS 2000 /* a read not back after this shows TO */

enum { DONGLE_MV, DONGLE_TEMP, DONGLE_READINGS };

static struct {
    uint16_t mv;
    bool temp_known;
    int8_t temp_c;
    uint8_t err[DONGLE_READINGS]; /* why not, as read_vdd_mv and read_temp say */
} dongle_readings; /* under debug_lock */
static atomic_t dongle_read_at[DONGLE_READINGS]; /* uptime ms of the last, 0 before the first */
/* a read under way (to tell one that never came back), and since when (uptime ms) */
static atomic_t dongle_reading[DONGLE_READINGS], dongle_reading_since[DONGLE_READINGS];

static K_THREAD_STACK_ARRAY_DEFINE(dongle_readings_stack, DONGLE_READINGS, DONGLE_READINGS_STACK);
static struct k_work_q dongle_readings_q[DONGLE_READINGS];
static void dongle_readings_cb(struct k_work *work);
static struct k_work_delayable dongle_readings_work[DONGLE_READINGS];

static void dongle_readings_cb(struct k_work *work) {
    const int which = k_work_delayable_from_work(work) == &dongle_readings_work[DONGLE_TEMP] ? DONGLE_TEMP : DONGLE_MV;
    atomic_set(&dongle_reading_since[which], (atomic_val_t)k_uptime_get_32());
    atomic_set(&dongle_reading[which], 1);
    uint8_t err = 0;
    if (which == DONGLE_TEMP) {
        int8_t temp_c = 0;
        const bool known = read_temp(&temp_c, &err);
        K_SPINLOCK(&debug_lock) {
            dongle_readings.temp_known = known;
            dongle_readings.temp_c = temp_c;
            dongle_readings.err[which] = err;
        }
    } else {
        const uint16_t mv = read_vdd_mv(&err);
        K_SPINLOCK(&debug_lock) {
            dongle_readings.mv = mv;
            dongle_readings.err[which] = err;
        }
    }
    atomic_set(&dongle_reading[which], 0);
    atomic_set(&dongle_read_at[which], (atomic_val_t)MAX(k_uptime_get_32(), 1));
}

/*
 * The readings that ask something else and wait for the answer: each link's
 * signal strength (a command to the radio), and how far the dongle has got
 * with each half's charging state. Taken once a second on a
 * thread of their own, so a slow or stuck answer can't hold up a frame on
 * the display queue (a frame drawn after a stuck one would never come,
 * leaving the screen blank). Always, whichever screen shows, so that the
 * signal and the typing speed have a history (FX_HISTORY seconds) when the
 * Keys or Status screen is turned to.
 */
#define READINGS_EVERY_MS 1000
#define READINGS_STACK 3072

static struct {
    struct fx_link link[3]; /* left, right, the computer: up, rssi_known, rssi */
    char half_reads[2][3];
    uint8_t half_err[2];
    int8_t signal_log[3][FX_HISTORY];
    uint8_t wpm_log[FX_HISTORY];
} readings; /* under debug_lock */

static K_THREAD_STACK_DEFINE(readings_stack, READINGS_STACK);
static struct k_work_q readings_q;
static void readings_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(readings_work, readings_cb);

static void readings_cb(struct k_work *work) {
    struct fx_link link[3] = {0};
    struct link_reading reading = {.link = {&link[0], &link[1], &link[2]}};
    bt_conn_foreach(BT_CONN_TYPE_LE, read_link, &reading);
    for (int i = 0; i < 3; i++) {
#if IS_ENABLED(CONFIG_BT_CTLR_CONN_RSSI)
        if (link[i].up) {
            link[i].rssi_err = reading.found[i] ? read_rssi(reading.handle[i], &link[i].rssi) : FX_RSSI_ERR_HANDLE;
            link[i].rssi_known = link[i].rssi_err == 0;
        }
#endif
    }
    /* where the dongle has got to with each half's charging state */
    char half_reads[2][3] = {{0}};
    uint8_t half_err[2] = {0};
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
    dongle_half_info_progress(0, half_reads[0], &half_err[0]);
    dongle_half_info_progress(1, half_reads[1], &half_err[1]);
#endif
    const bool usb_out = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    K_SPINLOCK(&debug_lock) {
        memcpy(readings.link, link, sizeof(link));
        memcpy(readings.half_reads, half_reads, sizeof(half_reads));
        memcpy(readings.half_err, half_err, sizeof(half_err));
        /* this second in the histories */
        for (int i = 0; i < 3; i++) {
            memmove(readings.signal_log[i], readings.signal_log[i] + 1, FX_HISTORY - 1);
            readings.signal_log[i][FX_HISTORY - 1] = i == 2 && usb_out ? 0
                                                     : !link[i].up     ? FX_SIGNAL_DOWN
                                                     : link[i].rssi_known ? (int8_t)CLAMP(link[i].rssi, -127, -1)
                                                                          : 0;
        }
        memmove(readings.wpm_log, readings.wpm_log + 1, FX_HISTORY - 1);
        readings.wpm_log[FX_HISTORY - 1] = (uint8_t)MIN(atomic_get(&wpm), 255);
    }
    k_work_schedule_for_queue(&readings_q, &readings_work, K_MSEC(READINGS_EVERY_MS));
}

/* Each Debug, Keys or Status frame: keep the dongle's own readings coming. */
static void readings_wanted(void) {
    /* the dongle's own: the first at once, then every few seconds (no change if already due) */
    for (int i = 0; i < DONGLE_READINGS; i++) {
        const uint32_t read_at = (uint32_t)atomic_get(&dongle_read_at[i]);
        const uint32_t since = k_uptime_get_32() - read_at;
        k_work_schedule_for_queue(&dongle_readings_q[i], &dongle_readings_work[i],
                                  read_at == 0 || since >= DONGLE_READINGS_EVERY_MS
                                      ? K_NO_WAIT
                                      : K_MSEC(DONGLE_READINGS_EVERY_MS - since));
    }
}

/* A link as read, over what the Debug screen keeps of it (its drops and their reason). */
static struct fx_link mix_link(struct fx_link kept, struct fx_link read) {
    kept.up = read.up;
    kept.rssi_known = read.rssi_known;
    kept.rssi = read.rssi;
    kept.rssi_err = read.rssi_err;
    return kept;
}

/* Why the dongle's own reading `which` isn't shown, for the Debug screen:
 * "TO" under way and not back, "WT" not read yet, "E1".. failed (see
 * read_temp, read_vdd_mv), "NB" not built in; "" when it was read. */
static void dongle_why(char why[3], uint8_t err, int which) {
    const uint32_t since = k_uptime_get_32() - (uint32_t)atomic_get(&dongle_reading_since[which]);
    if (atomic_get(&dongle_reading[which]) && since >= DONGLE_READ_STUCK_MS) {
        strcpy(why, "TO");
    } else if (atomic_get(&dongle_read_at[which]) == 0) {
        strcpy(why, "WT");
    } else if (err == 9) {
        strcpy(why, "NB");
    } else if (err) {
        why[0] = 'E', why[1] = (char)('0' + err % 10), why[2] = '\0';
    } else {
        why[0] = '\0';
    }
}

static void read_debug(void) {
    K_SPINLOCK(&debug_lock) {
        debug_shown = debug;
    }
    struct fx_debug *d = &debug_shown;
    d->uptime_s = (uint32_t)(k_uptime_get() / 1000);
    d->firmware = FIRMWARE_BUILD;
    d->reset_cause = reset_cause;
    d->crash = last_crash;
    d->crash_code = last_crash_code;
    d->crash_pc = last_crash_pc;
    d->activity = (uint8_t)zmk_activity_get_state();
    d->wpm = (uint16_t)atomic_get(&wpm);
    read_keys(d);
    d->usb = (uint8_t)zmk_usb_get_conn_state();
    d->usb_asleep = zmk_usb_get_status() == USB_DC_SUSPEND;
    for (int i = 0; i < FX_DEBUG_PROFILES; i++) {
        d->profile_state[i] = i >= ZMK_BLE_PROFILE_COUNT || zmk_ble_profile_is_open(i) ? 0
                              : zmk_ble_profile_is_connected(i) ? 2 : 1;
    }

#if IS_ENABLED(CONFIG_SYS_HEAP_RUNTIME_STATS) && !IS_ENABLED(CONFIG_LV_Z_MEM_POOL_HEAP_LIB_C)
    struct sys_memory_stats heap = {0};
    lvgl_heap_stats(&heap);
    const size_t total = heap.allocated_bytes + heap.free_bytes;
    if (total > 0) {
        d->heap_known = true;
        d->heap_used = (uint8_t)(heap.allocated_bytes * 100 / total);
        d->heap_peak = (uint8_t)MIN(heap.max_allocated_bytes * 100 / total, 100);
    }
#endif

    /* the links, signal strengths and the halves' charging reads: from the
     * readings thread, as last read */
    K_SPINLOCK(&debug_lock) {
        d->half[0] = mix_link(d->half[0], readings.link[0]);
        d->half[1] = mix_link(d->half[1], readings.link[1]);
        d->host = mix_link(d->host, readings.link[2]);
        memcpy(d->half_reads, readings.half_reads, sizeof(d->half_reads));
        memcpy(d->half_err, readings.half_err, sizeof(d->half_err));
        memcpy(d->signal_log, readings.signal_log, sizeof(d->signal_log));
        memcpy(d->wpm_log, readings.wpm_log, sizeof(d->wpm_log));
        d->dongle_mv = dongle_readings.mv;
        d->dongle_temp_known = dongle_readings.temp_known;
        d->dongle_temp_c = dongle_readings.temp_c;
        dongle_why(d->dongle_temp_why, dongle_readings.err[DONGLE_TEMP], DONGLE_TEMP);
        dongle_why(d->dongle_mv_why, dongle_readings.err[DONGLE_MV], DONGLE_MV);
    }
    readings_wanted();
    /* each half's battery volts and average draw */
    for (int half = 0; half < 2; half++) {
#if IS_ENABLED(CONFIG_DONGLE_HALF_INFO)
        d->half_mv[half] = dongle_half_millivolts(half);
        d->drain_hundredths_mw[half] = dongle_half_drain_hundredths_mw(half, &d->drain_rough[half]);
        d->battery_estimate[half] = dongle_half_charge_estimate(half, &d->estimating[half]);
#else
        d->drain_hundredths_mw[half] = -1;
#endif
    }
#if IS_ENABLED(CONFIG_ZMK_USB)
    d->usb_reports = dongle_usb_reports_sent();
    d->usb_errors = (uint16_t)MIN(dongle_usb_reports_failed(), UINT16_MAX);
#endif
}

/* Send what changed in the frame (or all of it) to the panel. */
static void show_frame(void) {
    for (int top = 0; top < FX_H; top += BAND_ROWS) {
        int x0 = 0, x1 = FX_W - 1;
        const bool text = overlay && !plain && top <= overlay_frame_row1() && top + BAND_ROWS > overlay_frame_row0();

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
            /* text that pulses, flashes or fades changes with the picture under it still */
            if (text) {
                x0 = MIN(x0, text_x0);
                x1 = MAX(x1, text_x1);
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
            memcpy(out + width, out, width * sizeof(*band));
            if (text) {
                overlay_blend_row(out, x0 * 2, y * 2, width);
                overlay_blend_row(out + width, x0 * 2, y * 2 + 1, width);
            }
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
    text_x0 = FX_W;
    text_x1 = -1;
}

/* The transition the frame is shown through, if it is: the panel going off
 * or coming on, or else a screensaver stage's. */
static bool stage_transition(uint32_t now, struct fx_transition *tr) {
    uint32_t into = now - stage_at;
    uint32_t length;

    if (power != POWER_STEADY) {
        tr->kind = power == POWER_GOING_OFF ? FX_TR_CRT_OFF : FX_TR_STATIC;
        length = power == POWER_GOING_OFF ? FX_POWER_OFF_MS : FX_POWER_ON_MS;
        tr->t = MIN((float)(now - power_at) / length, 1.0f);
        tr->frame = stage_frames;
        tr->fb = frame;
        tr->old = shown;
        tr->palette = native_palette;
        tr->old_palette = old_palette;
        return true;
    }
    switch (saver) {
    case SAVER_IN:
        tr->kind = fx_saver_way_in(screen - 1);
        length = FX_SAVER_IN_MS;
        break;
    case SAVER_OUT:
        if (into < FX_SAVER_GLITCH_FROM_MS) {
            return false; /* nothing yet: the ordinary way is quicker */
        }
        tr->kind = FX_TR_GLITCH;
        length = FX_SAVER_OUT_MS;
        break;
    case SAVER_CUT:
        tr->kind = FX_TR_CUT;
        length = FX_SAVER_CUT_MS;
        break;
    default:
        return false;
    }
    tr->t = MIN((float)into / length, 1.0f);
    tr->frame = stage_frames;
    tr->fb = frame;
    tr->old = shown;
    tr->palette = native_palette;
    tr->old_palette = old_palette;
    return true;
}

/* Send the whole frame to the panel as a transition has it. */
static void show_transition(const struct fx_transition *tr) {
    static uint16_t line[FX_W];

    for (int top = 0; top < FX_H; top += BAND_ROWS) {
        /* the text stays where it is, so not over a picture that is closing */
        const bool text = overlay && !plain && tr->kind != FX_TR_CRT_OFF && top <= overlay_frame_row1() &&
                          top + BAND_ROWS > overlay_frame_row0();
        uint16_t *out = band;
        for (int y = top; y < top + BAND_ROWS; y++) {
            fx_transition_row(tr, y, line);
            for (int x = 0; x < FX_W; x++) {
                out[x * 2] = out[x * 2 + 1] = sys_cpu_to_be16(line[x]);
            }
            memcpy(out + PANEL_W, out, PANEL_W * sizeof(*band));
            if (text) {
                overlay_blend_row(out, 0, y * 2, PANEL_W);
                overlay_blend_row(out + PANEL_W, 0, y * 2 + 1, PANEL_W);
            }
            out += PANEL_W * 2;
        }

        const struct display_buffer_descriptor desc = {
            .buf_size = BAND_PIXELS * sizeof(*band),
            .width = PANEL_W,
            .height = BAND_ROWS * 2,
            .pitch = PANEL_W,
        };
        display_write(panel, 0, top * 2, &desc, band);
    }
    text_x0 = FX_W;
    text_x1 = -1;
    redraw_all = true; /* for the first frame after it */
}

/* Send the frame: through the transition, if one is showing. */
static void present(uint32_t now) {
    struct fx_transition tr;
    if (stage_transition(now, &tr)) {
        show_transition(&tr);
    } else {
        show_frame();
    }
    stage_frames++;
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
    overlay = buf->data_size >= needed + OVERLAY_BYTES;
    if (overlay) {
        overlay_set_memory(buf->data + needed);
    } else {
        LOG_WRN("screen fx: no room for the status text");
    }
    return true;
}

/* Where each key position is on this keyboard, from the matrix transform
 * its keymap goes by (the physical layout's if it has one, as ZMK does), so
 * the animations put a key on the side of the screen it is on. */
#if DT_HAS_CHOSEN(zmk_physical_layout)
#define KEY_TRANSFORM DT_PHANDLE(DT_CHOSEN(zmk_physical_layout), transform)
#elif DT_HAS_CHOSEN(zmk_matrix_transform)
#define KEY_TRANSFORM DT_CHOSEN(zmk_matrix_transform)
#endif
#ifdef KEY_TRANSFORM
static const uint16_t key_places[] = DT_PROP(KEY_TRANSFORM, map);
#endif

/* Which key position pressing each encoder is, in the order of the dongle
 * overlay's &sensors (the halves' own order), so the Keys and Debug screens
 * draw those keys as knobs. -1 for an encoder with no push. */
#if IS_ENABLED(CONFIG_SHIELD_WATERFOWL_DONGLE)
/* left roller, left dial, right dial, right roller: the rollers' pushes at
 * the outer ends of the bottom row, the dials' inside them */
static const int8_t encoder_keys[] = {30, 34, 35, 39};
#define HAS_ENCODER_KEYS
/* ZMK's positive turn is anticlockwise on these, as the knob is seen */
#define ENCODER_TURN_SIGN -1
#elif IS_ENABLED(CONFIG_SHIELD_SPLITKB_AURORA_CORNE_DONGLE)
/* left, right: in place of the outer thumb keys */
static const int8_t encoder_keys[] = {36, 41};
#define HAS_ENCODER_KEYS
#endif
#ifndef ENCODER_TURN_SIGN
#define ENCODER_TURN_SIGN 1
#endif

static void begin_animation(void) {
    if (saver == SAVER_IN) {
        /* the still it comes in over: what was showing, or black */
        if (saver_over_module) {
            memset(shown, 0, PIXELS);
            memset(old_palette, 0, sizeof(old_palette));
        } else {
            memcpy(old_palette, native_palette, sizeof(old_palette));
        }
    }
    uint16_t *const native = native_palette;
#ifdef KEY_TRANSFORM
    fx_set_keys(key_places, ARRAY_SIZE(key_places));
#endif
#ifdef HAS_ENCODER_KEYS
    fx_set_encoders(encoder_keys, ARRAY_SIZE(encoder_keys));
#endif
    fx_set_scheme(atomic_get(&wanted_theme));
    fx_start(screen - 1, native);
    overlay_set_theme(fx_theme(screen - 1));
    is_debug = fx_is_debug(screen - 1);
    plain = is_debug || fx_shows_keys(screen - 1);
    for (int i = 0; i < 256; i++) {
        palette[i] = sys_cpu_to_be16(native[i]);
    }
    /* nothing typed before this screen came up belongs on it */
    K_SPINLOCK(&keys_lock) {
        n_keys = n_chars = 0;
    }
    redraw_all = true;
    last_frame_at = fps_at = k_uptime_get_32();
    frames_since = 0;
    status_at = last_frame_at - STATUS_MS;
    if (saver == SAVER_IN) {
        energy = 0;
    }
    if (saver != SAVER_OFF) {
        stage_at = last_frame_at;
        stage_frames = 0;
        fx_transition_begin(saver == SAVER_IN ? fx_saver_way_in(screen - 1) : FX_TR_CUT);
    }
}

static void apply_cb(struct k_work *work);
static K_WORK_DEFINE(apply_work, apply_cb);

static void request_apply(void);

/* The screensaver is over. A Screen key may have moved on meanwhile. */
static void saver_done(void) {
    saver = SAVER_OFF;
    redraw_all = true;
    if (atomic_get(&wanted_screen) != screen) {
        request_apply();
    }
}

/* The wanted screen comes back: an animation cuts in over the still in
 * `shown` (whose palette is in old_palette); the module's screen just does. */
static void saver_cut(void) {
    screen = atomic_get(&wanted_screen);
    if (screen == 0) {
        saver = SAVER_OFF;
        lv_screen_load(module_screen);
        lv_obj_invalidate(module_screen);
        return;
    }
    saver = SAVER_CUT;
    start_pending = true;
    schedule_frame(0);
}

/* Move the screensaver on a stage when it is time. True if this frame
 * isn't to be drawn: another screen is being started. */
static bool saver_step(uint32_t now) {
    const uint32_t into = now - stage_at;
    const bool key = (int32_t)((uint32_t)atomic_get(&last_key_at) - saver_began_at) > 0;

    switch (saver) {
    case SAVER_IN:
        if (key) {
            /* what is left of the screen's own picture is the still */
            saver_cut();
            return true;
        }
        if (into >= FX_SAVER_IN_MS) {
            saver = SAVER_ON;
        }
        break;
    case SAVER_ON:
        if (key) {
            saver = SAVER_OUT;
            stage_at = now;
            stage_frames = 0;
            fx_transition_begin(FX_TR_GLITCH);
        }
        break;
    case SAVER_OUT:
        if (into >= FX_SAVER_OUT_MS) {
            memcpy(shown, frame, PIXELS);
            memcpy(old_palette, native_palette, sizeof(old_palette));
            saver_cut();
            return true;
        }
        break;
    case SAVER_CUT:
        if (into >= FX_SAVER_CUT_MS) {
            saver_done();
        }
        break;
    }
    return false;
}

static void frame_cb(struct k_work *work) {
    if (screen == 0 || atomic_get(&paused)) {
        return;
    }
    atomic_inc(&frames_begun);

    if (start_pending) {
        if (!borrow_memory()) {
            /* back to the module's screen */
            first_picture();
            saver = SAVER_OFF;
            atomic_set(&wanted_screen, 0);
            k_work_submit_to_queue(zmk_display_work_q(), &apply_work);
            return;
        }
        begin_animation();
        start_pending = false;
    }

    const uint32_t now = k_uptime_get_32();
    if (saver != SAVER_OFF && saver_step(now)) {
        return;
    }
    if (atomic_set(&static_wanted, 0)) {
        power = POWER_COMING_ON;
        power_at = now;
        fx_transition_begin(FX_TR_STATIC);
        if (!lit) {
            /* the first picture: the backlight has waited for it */
            lit = true;
            dongle_backlight_on();
        }
    }
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
    K_SPINLOCK(&debug_lock) {
        in.held = debug.held;
    }

    /* energy: follows words per minute (quick up, slow down), and each key
     * nudges it straight away, since the speed is only worked out once a second */
    const float target = MIN(1.0f, atomic_get(&wpm) / (float)CONFIG_DONGLE_SCREEN_FX_WPM_REFERENCE);
    const float rate = (target > energy ? 0.12f : 0.03f) * in.step;
    energy += (target - energy) * MIN(rate, 1.0f) + in.n_keys * 0.03f;
    energy = CLAMP(energy, 0.0f, 1.0f);
    struct fx_transition tr;
    const bool in_transition = stage_transition(now, &tr);
    if (in_transition) {
        /* a way in may drive the animation itself, and feed it */
        const float driven = fx_transition_energy(tr.kind, tr.t);
        energy = driven >= 0 ? driven : energy;
        fx_transition_step(&tr);
    }
    in.energy = energy;
    in.wpm = (uint16_t)atomic_get(&wpm);

    if (now - status_at >= STATUS_MS) {
        status_at = now;
        read_status();
    }
    in.status = &status;
    if (plain) {
        /* the Debug and Keys screens: the readings, and nothing over them */
        static uint8_t fps;
        if (now - fps_at >= 1000) {
            fps = (uint8_t)MIN(frames_since * 1000 / (now - fps_at), 99);
            fps_at = now;
            frames_since = 0;
        }
        const uint8_t frame_ms = debug_shown.frame_ms;
        read_debug();
        debug_shown.fps = fps;
        debug_shown.frame_ms = frame_ms;
        in.debug = &debug_shown;
    } else if (overlay) {
        /* the text is only worked out again when it changes, and only the
         * parts of it that look different are sent again */
        const unsigned changed = overlay_update(&status, now);
        for (int area = 0; area < OVERLAY_AREAS; area++) {
            if (changed & BIT(area)) {
                int x, width;
                overlay_area_span(area, &x, &width);
                text_x0 = MIN(text_x0, MAX(x, 0) / 2);
                text_x1 = MAX(text_x1, MIN(x + width - 1, PANEL_W - 1) / 2);
            }
        }
    }

    fx_frame(screen - 1, frame, &in);
    if (notice != NOTICE_NONE && (int32_t)(notice_until - now) <= 0) {
        notice = NOTICE_NONE;
    }
    if (notice == NOTICE_THEME) {
        const int theme = atomic_get(&wanted_theme);
        char place[8];
        snprintf(place, sizeof(place), "%d/%d", theme + 1, fx_scheme_count());
        fx_draw_notice(frame, fx_scheme_name(theme), place);
    } else if (notice == NOTICE_BRIGHTNESS) {
        static const char *const names[DONGLE_BACKLIGHT_LEVELS] = {"LOW", "MEDIUM", "HIGH"};
        fx_draw_notice(frame, names[dongle_backlight_level()], "BRIGHTNESS");
    }
    present(now);
    first_picture();
    frames_since++;
    const uint32_t took = k_uptime_get_32() - now;
    debug_shown.frame_ms = (uint8_t)MIN(took, 255);

    if (power == POWER_GOING_OFF && now - power_at >= FX_POWER_OFF_MS) {
        /* that frame was black: nothing more until the panel is back on */
        power = POWER_STEADY;
        if (atomic_get(&sleep_wanted)) {
            atomic_set(&paused, 1);
            return;
        }
    } else if (power == POWER_COMING_ON && now - power_at >= FX_POWER_ON_MS) {
        power = POWER_STEADY;
    }

    if (is_debug && power == POWER_STEADY) {
        /* text doesn't need the animations' frame rate */
        schedule_frame(DEBUG_FRAME_MS);
        return;
    }
    /* always leave the queue a moment for LVGL's own tick and the backlight */
    schedule_frame(took + 5 < FRAME_MS ? FRAME_MS - took : 5);
}

/* The animation itself is set up by the first frame, once LVGL is done. */
static void start_animation(uint32_t delay_ms) {
    start_pending = true;
    schedule_frame(delay_ms);
}

/* Switch to the wanted screen, or pick up again after the keyboard was idle. */
/* Park LVGL on an empty screen: nothing on it ever needs redrawing. */
static bool leave_module_screen(void) {
    struct display_capabilities caps;
    display_get_capabilities(panel, &caps);
    if (caps.x_resolution != PANEL_W || caps.y_resolution != PANEL_H) {
        LOG_ERR("screen fx: expected a %dx%d panel, not %dx%d", PANEL_W, PANEL_H, caps.x_resolution,
                caps.y_resolution);
        return false;
    }
    module_screen = lv_screen_active();
    if (empty_screen == NULL) {
        empty_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(empty_screen, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(empty_screen, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_screen_load(empty_screen);
    return true;
}

static void apply_cb(struct k_work *work) {
    const int want = atomic_get(&wanted_screen);

    const bool dropped = atomic_set(&saver_drop, 0);
    if (dropped) {
        /* the keyboard has been idle, so the panel off (or on its way):
         * whatever shows next comes through static */
        power = POWER_STEADY;
        atomic_set(&static_wanted, 1);
    }
    if (saver != SAVER_OFF) {
        if (!dropped) {
            return; /* its way out ends on the wanted screen */
        }
        /* the panel has been off since: no show, straight to the screen */
        saver = SAVER_OFF;
    }

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
        if (!leave_module_screen()) {
            atomic_set(&wanted_screen, 0);
            return;
        }
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

/* --- the colour theme ------------------------------------------------------------- */

/* The Theme key: the screen showing takes the new colours as it is, if it
 * is one of those drawn in them, and the theme's name goes up on any. */
static void theme_cb(struct k_work *work) {
    fx_set_scheme(atomic_get(&wanted_theme));
    notice = NOTICE_THEME;
    notice_until = k_uptime_get_32() + NOTICE_MS;
    if (screen == 0 || start_pending || frame == NULL) {
        return; /* nothing of ours showing yet: begin_animation sets the theme too */
    }
    if (fx_recolor(screen - 1, native_palette)) {
        for (int i = 0; i < 256; i++) {
            palette[i] = sys_cpu_to_be16(native_palette[i]);
        }
        overlay_set_theme(fx_theme(screen - 1));
        redraw_all = true;
    }
}

static K_WORK_DEFINE(theme_work, theme_cb);

/* The Bright key: the backlight has changed already, this says to what. */
static void brightness_cb(struct k_work *work) {
    notice = NOTICE_BRIGHTNESS;
    notice_until = k_uptime_get_32() + NOTICE_MS;
}

static K_WORK_DEFINE(brightness_work, brightness_cb);

/* --- the panel going off ---------------------------------------------------------- */

/* The keyboard has gone idle: close the picture, if there is one of ours. */
static void sleep_cb(struct k_work *work) {
    if (!atomic_get(&sleep_wanted) || atomic_get(&paused)) {
        return;
    }
    if (!CLOSES_WHEN_IDLE || screen == 0 || start_pending || frame == NULL) {
        atomic_set(&paused, 1);
        return;
    }
    power = POWER_GOING_OFF;
    power_at = k_uptime_get_32();
    fx_transition_begin(FX_TR_CRT_OFF);
    schedule_frame(0);
}

static K_WORK_DEFINE(sleep_work, sleep_cb);

/* --- the screensaver: starting it ---------------------------------------------- */

#define SAVER_AFTER_MS (CONFIG_DONGLE_SCREEN_FX_SAVER_MINUTES * 60 * 1000)

/* Any key puts it off (and, through last_key_at, ends it). Any thread. */
static void saver_key(void) {
    atomic_set(&last_key_at, (atomic_val_t)k_uptime_get_32());
    if (SAVER_AFTER_MS > 0 && zmk_display_is_initialized()) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &saver_work, K_MSEC(SAVER_AFTER_MS));
    }
}

/* No key for long enough, or the lock key: one of the screensavers takes
 * over, picked at random. */
static void saver_cb(struct k_work *work) {
    if (saver != SAVER_OFF || atomic_get(&paused)) {
        return; /* the next key sets the wait off again */
    }
    if (screen != atomic_get(&wanted_screen) || start_pending) {
        /* a screen is still on its way up */
        k_work_reschedule_for_queue(zmk_display_work_q(), &saver_work, K_SECONDS(2));
        return;
    }

    /* chance: when the last key happened to be pressed, well stirred */
    uint32_t dice = ((uint32_t)atomic_get(&last_key_at) ^ k_cycle_get_32()) * 2654435761u;
    int choices = 0, choice = -1;
    for (int n = 0; n < fx_saver_count(); n++) {
        const int fx = fx_saver(n);
        /* each one so far is as likely as the next */
        dice = dice * 1103515245u + 12345u;
        if (fx + 1 != screen && (dice >> 16) % ++choices == 0) {
            choice = fx;
        }
    }
    if (choice < 0) {
        return;
    }

    uint32_t delay_ms = 0;
    saver_over_module = screen == 0;
    if (saver_over_module) {
        if (!leave_module_screen()) {
            return;
        }
        delay_ms = TAKEOVER_MS;
    }
    saver = SAVER_IN;
    saver_began_at = k_uptime_get_32();
    screen = choice + 1;
    start_animation(delay_ms);
}

/* --- the chosen screen, kept over a restart ----------------------------------- */

/*
 * Saved by name, so that adding or dropping an animation doesn't turn the
 * saved one into its neighbour. Only once the screen has been left showing
 * for SAVE_DELAY_MS: stepping through them all writes to flash once. The
 * colour theme is kept the same way, after the same wait, and so is the
 * backlight's brightness (src/dongle_idle_backlight.c), as its level.
 */
#define STATUS_NAME "STATUS" /* the module's screen */

static const char *screen_name(int which) { return which == 0 ? STATUS_NAME : fx_name(which - 1); }

#if IS_ENABLED(CONFIG_SETTINGS)

#define SAVED_SETTING "screen_fx/screen"
#define SAVED_THEME_SETTING "screen_fx/theme"
#define SAVED_BRIGHTNESS_SETTING "screen_fx/brightness"
#define SAVED_NAME_SIZE 16

/* what flash holds; empty: nothing saved */
static char saved_name[SAVED_NAME_SIZE], saved_theme[SAVED_NAME_SIZE];
static uint8_t saved_brightness = DONGLE_BACKLIGHT_MEDIUM; /* or what there is until one is */

static int saved_screen_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (settings_name_steq(key, "brightness", NULL)) {
        uint8_t level;
        if (len != sizeof(level)) {
            return -EINVAL;
        }
        const ssize_t got = read_cb(cb_arg, &level, sizeof(level));
        if (got < 0) {
            return (int)got;
        }
        saved_brightness = level;
        dongle_backlight_set_level(level); /* one there no longer is leaves medium */
        return 0;
    }
    const bool theme = settings_name_steq(key, "theme", NULL);
    char *const saved = theme ? saved_theme : saved_name;

    if (!theme && !settings_name_steq(key, "screen", NULL)) {
        return -ENOENT;
    }
    if (len >= SAVED_NAME_SIZE) {
        return -EINVAL;
    }
    const ssize_t got = read_cb(cb_arg, saved, len);
    if (got < 0) {
        saved[0] = '\0';
        return (int)got;
    }
    saved[got] = '\0';
    /* a theme there no longer is leaves the first */
    for (int which = 0; theme && which < fx_scheme_count(); which++) {
        if (strcmp(saved, fx_scheme_name(which)) == 0) {
            atomic_set(&wanted_theme, which);
        }
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(dongle_screen_fx, "screen_fx", NULL, saved_screen_set, NULL, NULL);

/* Write `name` to `setting` unless `saved`, what flash has, is that already. */
static void save_name(const char *setting, const char *name, char *saved) {
    if (strcmp(name, saved) == 0 || strlen(name) >= SAVED_NAME_SIZE) {
        return;
    }
    const int err = settings_save_one(setting, name, strlen(name));
    if (err != 0) {
        LOG_ERR("screen fx: saving %s failed (%d)", setting, err);
        return;
    }
    strcpy(saved, name);
}

static void save_cb(struct k_work *work) {
    save_name(SAVED_SETTING, screen_name(atomic_get(&wanted_screen)), saved_name);
    save_name(SAVED_THEME_SETTING, fx_scheme_name(atomic_get(&wanted_theme)), saved_theme);

    const uint8_t brightness = (uint8_t)dongle_backlight_level();
    if (brightness != saved_brightness) {
        const int err = settings_save_one(SAVED_BRIGHTNESS_SETTING, &brightness, sizeof(brightness));
        if (err != 0) {
            LOG_ERR("screen fx: saving the brightness failed (%d)", err);
            return;
        }
        saved_brightness = brightness;
    }
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_cb);

#endif

/* A screen the keys step through: not one of the screensavers, and the
 * module's own only if asked for. */
static bool can_choose(int which) {
    return which == 0 ? IS_ENABLED(CONFIG_DONGLE_SCREEN_FX_MODULE_SCREEN) : fx_saver_way_in(which - 1) == FX_TR_NONE;
}

/* The screen to start on: the saved one if it still exists, or -1. */
static int saved_screen(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    for (int which = 0; which <= fx_count(); which++) {
        if (can_choose(which) && strcmp(saved_name, screen_name(which)) == 0) {
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
    int want = atomic_get(&wanted_screen);
    do {
        want = (want + (by < 0 ? screens - 1 : 1)) % screens;
    } while (!can_choose(want));
    atomic_set(&wanted_screen, want);
    request_apply();
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_DONGLE_SCREEN_FX_SAVE_DELAY_MS));
#endif
}

void dongle_screen_fx_theme_step(int by) {
    if (!zmk_display_is_initialized()) {
        return;
    }
    const int themes = fx_scheme_count();
    atomic_set(&wanted_theme, (atomic_get(&wanted_theme) + (by < 0 ? themes - 1 : 1)) % themes);
    k_work_submit_to_queue(zmk_display_work_q(), &theme_work);
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_DONGLE_SCREEN_FX_SAVE_DELAY_MS));
#endif
}

void dongle_screen_fx_brightness_step(void) {
    if (!zmk_display_is_initialized()) {
        return;
    }
    dongle_backlight_step();
    k_work_submit_to_queue(zmk_display_work_q(), &brightness_work);
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

/* Add to the Debug screen's log of raw events. Call with debug_lock held. */
static void log_event(struct fx_event event, uint32_t now) {
    event.gap_ms = (uint16_t)MIN(now - last_event_at, 9999);
    last_event_at = now;
    if (debug.n_log == FX_DEBUG_LOG) {
        memmove(&debug.log[0], &debug.log[1], sizeof(debug.log) - sizeof(debug.log[0]));
        debug.n_log--;
    }
    debug.log[debug.n_log++] = event;
}

/* The modifiers a key code is or carries, if it is a modifier: &kp LSHIFT, &kp LC(LSHIFT). */
static uint8_t mods_of(uint32_t encoded) {
    const uint16_t page = ZMK_HID_USAGE_PAGE(encoded), id = ZMK_HID_USAGE_ID(encoded);
    if (!is_mod(page ? page : HID_USAGE_KEY, id)) {
        return 0;
    }
    return (uint8_t)(SELECT_MODS(encoded) | BIT(id - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL));
}

/* A key has gone down: note what its binding can hold. As the keymap does,
 * that is the one on the highest layer that is on and isn't &trans there. */
static void key_pressed(uint32_t key) {
    uint8_t holds = HOLDS_NOTHING, what = 0;
    for (int index = ZMK_KEYMAP_LAYERS_LEN - 1; index >= 0; index--) {
        const zmk_keymap_layer_id_t layer = zmk_keymap_layer_index_to_id(index);
        if (layer == ZMK_KEYMAP_LAYER_ID_INVAL || !(zmk_keymap_layer_active(layer) || index == 0)) {
            continue;
        }
        const struct zmk_behavior_binding *binding = zmk_keymap_get_layer_binding_at_idx(layer, key);
        if (binding == NULL || binding->behavior_dev == NULL) {
            continue;
        }
        uint8_t kind = HOLDS_NOTHING;
        for (int i = 0; i < ARRAY_SIZE(behavior_kinds); i++) {
            if (strcmp(binding->behavior_dev, behavior_kinds[i].name) == 0) {
                kind = behavior_kinds[i].kind;
                break;
            }
        }
        if (kind == FALLS_THROUGH) {
            continue;
        }
        if (kind == HOLDS_MODS) {
            what = mods_of(binding->param1);
            holds = what ? HOLDS_MODS : HOLDS_NOTHING;
        } else if (kind == HOLDS_LAYER) {
            what = (uint8_t)binding->param1;
            holds = HOLDS_LAYER;
        }
        break;
    }
    key_holds[key] = holds;
    key_holds_what[key] = what;
}

/* What the Keys screen shows, and part of what the Debug screen does. */
static void read_keys(struct fx_debug *d) {
    uint64_t held = 0;
    K_SPINLOCK(&debug_lock) {
        held = debug.held;
    }
    d->held = held;
    d->mods = zmk_hid_get_explicit_mods();
    d->layer = zmk_keymap_highest_layer_active();
    d->layer_name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(d->layer));
    d->layers = zmk_keymap_layer_state();
    d->keyboard = CONFIG_ZMK_KEYBOARD_NAME;
    d->battery_mah = CONFIG_HALF_BATTERY_MAH;
    d->held_mod = d->held_layer = 0;
    for (int key = 0; key < KEY_INFOS; key++) {
        if (!(held & BIT64(key))) {
            continue;
        }
        if (key_holds[key] == HOLDS_MODS && (d->mods & key_holds_what[key]) == key_holds_what[key]) {
            d->held_mod |= BIT64(key);
        } else if (key_holds[key] == HOLDS_LAYER && zmk_keymap_layer_active(key_holds_what[key])) {
            d->held_layer |= BIT64(key);
        }
    }
}

static void debug_position(const struct zmk_position_state_changed *pos) {
    const uint32_t now = k_uptime_get_32(), key = pos->position;
    if (pos->state && key < KEY_INFOS) {
        key_pressed(key);
    }
    K_SPINLOCK(&debug_lock) {
        if (key < 64) {
            debug.held = (debug.held & ~BIT64(key)) | (pos->state ? BIT64(key) : 0);
        }
        if (pos->state && pos->source < 2) {
            debug.presses[pos->source]++;
        }
        if (key < BOUNCE_KEYS) {
            if (!pos->state) {
                released_at[key] = now | 1;
            } else if (released_at[key] != 0) {
                /* events from a half arrive a connection interval apart at
                 * best, so this can't see a bounce shorter than that */
                const uint32_t gap = MAX(now - released_at[key], 1);
                if (gap < BOUNCE_WITHIN_MS && (debug.bounce_ms == 0 || gap < debug.bounce_ms)) {
                    debug.bounce_ms = (uint16_t)gap;
                    debug.bounce_key = (uint8_t)key;
                }
            }
        }
        log_event((struct fx_event){.kind = 'P', .down = pos->state, .source = pos->source, .a = (uint16_t)key}, now);
    }
}

/* Which half a connection is: the slot ZMK keeps its address in. */
static int half_of(struct bt_conn *conn) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE || info.role != BT_CONN_ROLE_CENTRAL) {
        return -1;
    }
    const int slot = zmk_ble_put_peripheral_addr(bt_conn_get_dst(conn));
    return slot >= 0 && slot < 2 ? slot : -1;
}

static void link_connected(struct bt_conn *conn, uint8_t err) {
    const int half = err ? -1 : half_of(conn);
    if (half >= 0) {
        K_SPINLOCK(&debug_lock) {
            half_conn[half] = conn;
        }
    }
}

static void link_disconnected(struct bt_conn *conn, uint8_t reason) {
    K_SPINLOCK(&debug_lock) {
        struct fx_link *link = conn == half_conn[0] ? &debug.half[0] : conn == half_conn[1] ? &debug.half[1] : NULL;
        if (link != NULL) {
            half_conn[link == &debug.half[1]] = NULL;
        } else {
            struct bt_conn_info info;
            /* a computer, on any profile; not a half that never got as far as its slot */
            if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL) {
                link = &debug.host;
            }
        }
        if (link != NULL) {
            link->drops = MIN(link->drops + 1, UINT16_MAX);
            link->reason = reason;
        }
    }
}

BT_CONN_CB_DEFINE(screen_fx_links) = {
    .connected = link_connected,
    .disconnected = link_disconnected,
};

static int screen_fx_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        debug_position(pos);
        if (pos->state) {
            saver_key();
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
        K_SPINLOCK(&debug_lock) {
            log_event((struct fx_event){.kind = 'K', .down = code->state, .a = code->usage_page,
                                        .b = (uint16_t)code->keycode,
                                        .mods = code->implicit_modifiers | code->explicit_modifiers},
                      k_uptime_get_32());
        }
        /* the Lower layer's lock key (Ctrl+Cmd+Q, a Mac's Lock Screen)
         * locks the computer, so the screensaver comes on with it instead of
         * 15 minutes later: when the key is let go. The computer's power key
         * (C_PWR) does the same, should a keymap have one. Not if the
         * screensaver is turned off (SAVER_MINUTES 0). */
        const uint8_t lock_mods = MOD_LCTL | MOD_LGUI;
        const bool lock_key = code->usage_page == HID_USAGE_KEY &&
                              code->keycode == HID_USAGE_KEY_KEYBOARD_Q &&
                              (code->implicit_modifiers & lock_mods) == lock_mods;
        const bool power_key = code->usage_page == HID_USAGE_CONSUMER &&
                               code->keycode == HID_USAGE_CONSUMER_POWER;
        if (SAVER_AFTER_MS > 0 && !code->state && (lock_key || power_key) &&
            zmk_display_is_initialized()) {
            k_work_reschedule_for_queue(zmk_display_work_q(), &saver_work, K_NO_WAIT);
        }
        const char typed = code->state ? typed_char(code) : 0;
        if (typed != 0) {
            dongle_wpm_typed(typed);
        }
        /* only kept while an animation shows, and only until its next frame */
        const char c = atomic_get(&wanted_screen) != 0 ? typed : 0;
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

    const struct zmk_sensor_event *sensor = as_zmk_sensor_event(eh);
    if (sensor != NULL) {
        saver_key(); /* an encoder counts */
        K_SPINLOCK(&debug_lock) {
            if (sensor->sensor_index < FX_MAX_ENCODERS) {
                debug.turned[sensor->sensor_index] += ENCODER_TURN_SIGN * (int16_t)sensor->channel_data[0].value.val1;
            }
            log_event((struct fx_event){.kind = 'E', .source = 2, .a = sensor->sensor_index,
                                        .b = (uint16_t)(int16_t)sensor->channel_data[0].value.val1},
                      k_uptime_get_32());
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);
    if (activity != NULL) {
        /* the panel is switched off while idle (src/dongle_idle_backlight.c),
         * once the picture has closed: sleep_cb */
        const bool idle = activity->state != ZMK_ACTIVITY_ACTIVE;
        if (idle) {
            atomic_set(&saver_drop, 1);
            if (!atomic_set(&sleep_wanted, 1)) {
                if (zmk_display_is_initialized()) {
                    k_work_submit_to_queue(zmk_display_work_q(), &sleep_work);
                } else {
                    atomic_set(&paused, 1);
                }
            }
        } else if (atomic_set(&sleep_wanted, 0)) {
            atomic_set(&paused, 0);
            request_apply();
            atomic_set(&frames_at_wake, atomic_get(&frames_begun));
            k_work_reschedule(&wake_watch_work, K_MSEC(FIRST_PICTURE_BY_MS));
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
ZMK_SUBSCRIPTION(dongle_screen_fx, zmk_sensor_event);

/* --- the screen to start on ----------------------------------------------------- */

static void start_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(start_work, start_cb);

static void start_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        k_work_schedule(&start_work, K_MSEC(500));
        return;
    }
    saver_key(); /* the wait for the screensaver starts here */
    if (atomic_get(&chosen)) {
        return; /* a key got there first */
    }
    /* DONGLE_SCREEN_FX_START counts the screens the keys step through; the
     * first of them if there aren't that many */
    int start = -1;
    for (int which = 0, nth = 0; which <= fx_count(); which++) {
        if (can_choose(which) && (nth++ == CONFIG_DONGLE_SCREEN_FX_START || start < 0)) {
            start = which;
        }
    }
    int saved = saved_screen();
    if (safe_start) {
        /* the last start showed nothing: the first animation this time */
        saved = -1;
        for (int which = 1; which <= fx_count(); which++) {
            if (can_choose(which) && !fx_is_debug(which - 1) && !fx_shows_keys(which - 1)) {
                saved = which;
                break;
            }
        }
    }
    atomic_set(&wanted_screen, saved >= 0 ? saved : start);
    request_apply();
}

static void wake_watch_cb(struct k_work *work) {
    if (atomic_get(&sleep_wanted) || atomic_get(&wanted_screen) == 0 ||
        atomic_get(&frames_begun) != atomic_get(&frames_at_wake)) {
        return; /* idle again, on the module's screen, or drawing */
    }
    boot_note.crash = FX_CRASH_HANG;
    boot_note.code = FX_HANG_AFTER_WAKE;
    boot_note.troubled = 1; /* the first animation next time, once */
    boot_note_keep();
    sys_reboot(SYS_REBOOT_WARM);
}

static int screen_fx_init(void) {
    boot_note_take();
#if !IS_ENABLED(CONFIG_DONGLE_SCREEN_FX_MODULE_SCREEN)
    k_work_schedule(&picture_watch_work, K_MSEC(FIRST_PICTURE_BY_MS));
#endif
    k_work_queue_start(&readings_q, readings_stack, K_THREAD_STACK_SIZEOF(readings_stack),
                       K_LOWEST_APPLICATION_THREAD_PRIO, &(struct k_work_queue_config){.name = "screen_fx_read"});
    k_work_schedule_for_queue(&readings_q, &readings_work, K_MSEC(READINGS_EVERY_MS));
    static const char *const dongle_q_names[DONGLE_READINGS] = {"screen_fx_mv", "screen_fx_temp"};
    for (int i = 0; i < DONGLE_READINGS; i++) {
        k_work_init_delayable(&dongle_readings_work[i], dongle_readings_cb);
        k_work_queue_start(&dongle_readings_q[i], dongle_readings_stack[i], K_THREAD_STACK_SIZEOF(dongle_readings_stack[i]),
                           K_LOWEST_APPLICATION_THREAD_PRIO, &(struct k_work_queue_config){.name = dongle_q_names[i]});
    }
    /* after the module's screen has drawn itself once, and ZMK has loaded
     * the settings (the saved screen among them) */
    k_work_schedule(&start_work, K_MSEC(1500));
#if IS_ENABLED(CONFIG_HWINFO)
    /* why the dongle last restarted, for the Debug screen; cleared, or the
     * chip would keep adding every restart since power-up to it */
    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
        hwinfo_clear_reset_cause();
    }
#endif
    return 0;
}

SYS_INIT(screen_fx_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
