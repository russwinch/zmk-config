/*
 * Animations for the Prospector dongle's screen.
 *
 * Plain C with no Zephyr in it, so the same code draws on the dongle
 * (src/dongle_screen_fx.c) and on a computer (tools/fx_preview.py, which
 * renders each animation to a GIF).
 *
 * An animation draws into a 140x120 picture of palette indexes, which the
 * dongle shows doubled on its 280x240 panel: a quarter of the pixels to
 * work out, and one byte each.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FX_W 140
#define FX_H 120

/* The panel's corners are rounded off, about 43 of its pixels across (the
 * radius Adafruit gives for the same 1.69" 280x240 glass), so 21.5 of ours:
 * what is drawn there isn't seen. */
#define FX_CORNER_R 21.5f

/* How many columns in from either side rows `y0` to `y1` have to start to
 * be wholly on the panel: 0 away from the corners. */
int fx_corner_inset(int y0, int y1);

/* Palette entries 0..251 belong to the animation; these are fixed. */
#define FX_COLORS 252
#define FX_BLACK 252
#define FX_RED 253
#define FX_GREY 254
#define FX_WHITE 255

/* A battery at FX_WARN_BATTERY percent or under is shown in amber. At
 * FX_LOW_BATTERY or under it is red and flashes slowly, a second on and a
 * second off; at FX_FLAT_BATTERY or under, fast: twice a second. */
#define FX_WARN_BATTERY 15
#define FX_LOW_BATTERY 10
#define FX_FLAT_BATTERY 5

/* Whether a battery at `percent` (0: not known) is in the off half of its
 * flash at `ms`. */
static inline bool fx_battery_flash_off(uint8_t percent, uint32_t ms) {
    return percent == 0 || percent > FX_LOW_BATTERY ? false
           : percent > FX_FLAT_BATTERY               ? ms % 2000 >= 1000
                                                     : ms % 500 >= 250;
}

#define FX_MAX_KEYS 8
#define FX_MAX_CHARS 8

struct fx_status {
    uint8_t battery[2];     /* left, right: percent, 0 = not known */
    bool half_connected[2]; /* left, right */
    bool charging[2];       /* left, right: on USB power: a bolt beside its level (which reads high
                             * while charging: the charging voltage) */
    bool bolt_off[2];       /* left, right: charging, below 100, and the bolt in the off half of its flash */
    uint8_t settling[2];    /* left, right: just unplugged, its level still the charger's:
                             * a spinner frame (FX_SPIN_FRAMES), 0 when not */
    bool host_connected;    /* the computer */
    bool usb;               /* typing over USB, not Bluetooth */
    bool host_open;         /* Bluetooth: nothing paired on this profile */
    bool profile_connected; /* the Bluetooth profile's computer is connected, whichever output is in use */
    uint8_t profile;        /* Bluetooth: profile number, from 0 */
};

/* Colours (RGB565) for the status text drawn over an animation
 * (status_overlay.c), chosen to suit its picture. Not the colour themes
 * the Theme key steps through: those are the "schemes" below, and an
 * animation drawn in a scheme's colours has its text in them too. */
struct fx_theme {
    uint16_t ink;    /* the battery levels, and the Bluetooth profile number */
    uint16_t dim;    /* the profile number when the computer isn't connected */
    uint16_t accent; /* the wireless symbol when it is */
    uint16_t search; /* the symbol when it isn't: this one pulses */
    uint16_t low;    /* a battery that is nearly flat */
    uint16_t halo;   /* around all of it: dark for light text, light for dark */
    uint8_t font;    /* FX_FONT_FIELD, or FX_FONT_PIXEL where the picture has letters of its own */
    uint8_t drop;    /* panel rows to move it all down by (and two thirds of that in from the sides) */
    bool dots;       /* no levels and no output: only a battery, in the place of the level of one that is low */
};

#define FX_FONT_FIELD 0 /* the Prospector module's Field screen fonts */
#define FX_FONT_PIXEL 1 /* the animations' own 5x7 letters, three panel pixels to a dot */

/* --- what the Debug screen shows -------------------------------------------- */

#define FX_DEBUG_LOG 3
#define FX_DEBUG_PROFILES 5
#define FX_MAX_ENCODERS 4

#define FX_DRAIN_UNKNOWN 0xffff

#define FX_CRASH_HANG 1
#define FX_CRASH_FAULT 2
/* crash_code for FX_CRASH_HANG: when no picture came */
#define FX_HANG_AT_START 1
#define FX_HANG_AFTER_WAKE 2

/* fx_link.rssi_err beyond an error number */
#define FX_RSSI_ERR_HANDLE 254
#define FX_RSSI_ERR_BUFFER 255

/* A Bluetooth link: to a half, or to the computer. */
struct fx_link {
    bool up;
    bool rssi_known;
    int8_t rssi;       /* dBm, as the dongle hears the other end */
    uint8_t rssi_err;  /* up, but its signal couldn't be read: why (fx.c's debug_link), else 0 */
    uint16_t drops;    /* times it has been lost since power-up */
    uint8_t reason;    /* Bluetooth's reason for the last loss (0: none yet) */
};

struct fx_event {
    char kind;       /* 'P' key position, 'K' keycode, 'E' encoder */
    bool down;
    uint8_t source;  /* P: 0 left, 1 right */
    uint8_t mods;    /* K: modifiers sent with it */
    uint16_t a, b;   /* P: position. K: usage page, id. E: sensor, steps (signed) */
    uint16_t gap_ms; /* since the event before it, 9999 at most */
};

struct fx_debug {
    const char *firmware; /* which build this is, short ("B149"), or NULL */
    uint32_t uptime_s;
    uint32_t reset_cause; /* Zephyr's RESET_* bits */
    uint8_t activity;     /* 0 active, 1 idle, 2 asleep */
    uint8_t fps, frame_ms; /* this screen: frames a second, time to draw and send one */
    bool heap_known;
    uint8_t heap_used, heap_peak; /* LVGL's memory, percent */
    struct fx_link half[2], host;
    /* how the dongle's last run ended, beyond reset_cause: 0, FX_CRASH_HANG
     * (no picture within 15 s of power-up, or of waking, so it restarted
     * itself: crash_code FX_HANG_AT_START or FX_HANG_AFTER_WAKE) or
     * FX_CRASH_FAULT (crashed, with Zephyr's reason in crash_code) */
    uint8_t crash, crash_code;
    uint16_t drain_tenths[2]; /* each half's average draw, tenths of a percent an hour (FX_DRAIN_UNKNOWN) */
    uint8_t profile_state[FX_DEBUG_PROFILES]; /* 0 open, 1 paired, 2 connected */
    uint8_t usb;          /* 0 no, 1 powered, 2 ready to type */
    bool usb_asleep;      /* the computer has suspended the port: it is asleep */
    uint8_t layer;        /* the highest one active */
    const char *layer_name; /* or NULL */
    uint32_t layers;      /* a bit for each active layer */
    uint8_t mods;         /* held: HID modifier bits */
    uint16_t wpm;
    uint64_t held;        /* a bit for each key position held */
    uint64_t held_mod;    /* those of them holding a modifier down */
    uint64_t held_layer;  /* and those holding a layer on */
    const char *keyboard; /* its name, or NULL */
    int16_t turned[FX_MAX_ENCODERS]; /* each encoder's turn since power-up, by sensor (+ clockwise), in the
                                      * whole degrees its events carry (ZMK's EC11: 360 / steps a pulse,
                                      * 4 on the Waterfowl); wraps round */
    uint32_t presses;     /* key presses since power-up */
    uint16_t bounce_ms;   /* shortest release-to-press of one key (0: none seen) */
    uint8_t bounce_key;
    uint32_t usb_reports; /* keyboard and media reports sent over USB since power-up */
    uint16_t usb_errors;  /* and those that failed (the computer not taking them) */
    uint16_t mv[3];       /* supplies, millivolts (0: not known): the left half's battery, the
                           * dongle's 3.3 V regulator, the right half's battery */
    bool temp_known[3];   /* chip temperatures: left half, dongle, right half */
    int8_t temp_c[3];
    bool tx_known[3];     /* radio transmit power, as built, dBm: the same three */
    int8_t tx_dbm[3];
    struct fx_event log[FX_DEBUG_LOG]; /* the newest last */
    uint8_t n_log;
};

struct fx_input {
    float step;                /* time since the last frame, in 30ths of a second */
    float energy;              /* 0..1: how hard you are typing */
    uint16_t wpm;              /* words per minute, as ZMK counts them */
    uint8_t n_keys;            /* keys pressed since the last frame */
    uint8_t keys[FX_MAX_KEYS]; /* their key positions */
    uint64_t held;             /* a bit for each key position held down now */
    uint8_t n_chars;           /* characters typed since the last frame */
    char chars[FX_MAX_CHARS];  /* printable ASCII, '\n' or '\b' */
    const struct fx_status *status; /* for an animation that shows some of it itself */
    const struct fx_debug *debug;   /* for the Debug and Keys screens (fx_is_debug, fx_shows_keys) */
};

/* Where the board's key positions are: for each, its row << 8 | column, as
 * in ZMK's matrix transform (RC). The animations place a key by it and the
 * Debug screen draws the board from it. Kept, not copied; without it the
 * positions are taken to be the Urchin's (and Forager's). */
void fx_set_keys(const uint16_t *places, int count);

/* The board's encoders, by sensor index: the key position pressing each one
 * is, or -1 for one that has none. The Keys and Debug screens draw those
 * positions as knobs, turning with fx_debug's `turned`. Copied; without it
 * the board has no encoders. */
void fx_set_encoders(const int8_t *keys, int count);

int fx_count(void);
const char *fx_name(int fx);

const struct fx_theme *fx_theme(int fx);

/* The 7 rows of a character in the animations' letters (top first, left dot
 * in bit 4), or NULL if there is none. */
const uint8_t *fx_glyph_rows(char c);

/* In fx_glyph_rows (as "\x7f" in a string): a lightning bolt, shown beside
 * the level of a half on USB power: flashing while under 100, solid at 100
 * (fx_status.bolt_off), and alone while no level is known. */
#define FX_GLYPH_BOLT '\x7f'
#define FX_BOLT "\x7f"

/* Also in fx_glyph_rows: the Mac's modifier symbols, control, option,
 * shift and command. */
#define FX_GLYPH_CTRL '\x01'
#define FX_GLYPH_ALT '\x02'
#define FX_GLYPH_SHIFT '\x03'
#define FX_GLYPH_GUI '\x04'

/* Also in fx_glyph_rows: a spinner's frames (| / - \\), shown for a half
 * just unplugged until its battery is read again (fx_status.settling counts
 * 1 to FX_SPIN_FRAMES). */
#define FX_GLYPH_SPIN '\x05'
#define FX_SPIN_FRAMES 4

/* A battery level's text for fx_status.settling's frame (1 on). */
static inline void fx_spin_text(char *p, uint8_t frame) {
    p[0] = (char)(FX_GLYPH_SPIN + (frame - 1) % FX_SPIN_FRAMES);
    p[1] = '\0';
}

/* A character only the pixel letters have: the bolt, the spinner. */
static inline bool fx_glyph_is_symbol(char c) {
    return c == FX_GLYPH_BOLT || (c >= FX_GLYPH_SPIN && c < FX_GLYPH_SPIN + FX_SPIN_FRAMES);
}

/* The Debug screen: wants fx_input's debug filled in, fills the whole
 * picture with its own text, and takes no status text over it. */
bool fx_is_debug(int fx);

/* The Keys screen: the same goes for it, but it is drawn as often as an
 * animation is. */
bool fx_shows_keys(int fx);

/* FX_W * FX_H bytes for the animations to keep between frames. Call before
 * fx_start, and again if the memory moves. */
void fx_set_canvas(uint8_t *memory);

/* Start animation `fx`: resets it, fills `palette` (256 x RGB565). */
void fx_start(int fx, uint16_t *palette);

/*
 * Colour themes: the list is in colors.h, with every other colour. The
 * Keys, Debug, Tracker and Terminal screens are drawn in the colours of the one that is set (the first, until another
 * is); the other animations have their own. Setting one changes nothing
 * that is showing: the next fx_start has it, and so does fx_recolor, which
 * fills `palette` in again without starting the animation afresh. That one
 * returns false, with the palette as it was, for an animation that isn't in
 * the theme's colours. fx_theme follows the theme too, so set the status
 * text's colours again after either.
 */
int fx_scheme_count(void);
const char *fx_scheme_name(int scheme);
void fx_set_scheme(int scheme);
bool fx_recolor(int fx, uint16_t *palette);

/* A boxed notice in the middle of a frame, over whatever is drawn there:
 * `title` in letters twice the size, `detail` ("" for none) small under
 * it. In the fixed colours, so it reads on any animation. */
void fx_draw_notice(uint8_t *fb, const char *title, const char *detail);

/* Draw the next frame into `fb` (FX_W * FX_H palette indexes). */
void fx_frame(int fx, uint8_t *fb, const struct fx_input *in);

/* --- the screensaver (the dongle starts it when no key has been pressed for
 * a while) ---------------------------------------------------------------- */

/*
 * Some animations can be the screensaver: each has its own way in, which
 * distorts the picture that was showing into it. The way out is the same
 * for all: from the first key the screensaver's own picture glitches, more
 * and more (FX_TR_GLITCH), and at the end the screen that was showing
 * before cuts back in through it, block by block (FX_TR_CUT).
 *
 * Only one animation runs at a time. The other picture is a still: the last
 * frame of the animation that stopped (`old`, palette indexes) and the
 * palette it had. A way in may write to that still.
 */
#define FX_SAVER_IN_MS 18000         /* the way in */
#define FX_SAVER_OUT_MS 8000         /* from the first key: the screensaver, breaking up... */
#define FX_SAVER_GLITCH_FROM_MS 2000 /* ...from here on */
#define FX_SAVER_CUT_MS 1000         /* then the screen coming back */

enum {
    FX_TR_NONE,
    FX_TR_HYPERSPACE, /* into Warp: torn open at the vanishing point, streaking past */
    FX_TR_INFECT,     /* into Life: the picture's bright parts come alive and eat it */
    FX_TR_MELT,       /* into Code: it runs down the screen like wax, and only then is anything written */
    FX_TR_GLITCH,     /* `old_palette` is the screen to go back to; `old` isn't used */
    FX_TR_CUT,        /* `old` is the screensaver's last frame */
    FX_TR_CRT_OFF,    /* the panel going off: the picture closes to a line, then a dot, like an old television */
    FX_TR_STATIC,     /* the panel coming on: white noise that the picture comes through */
};

/* How long those two take. The backlight goes down after the first, and
 * comes up during the second (src/dongle_idle_backlight.c). */
#define FX_POWER_OFF_MS 650
#define FX_POWER_ON_MS 1200

struct fx_transition {
    uint8_t kind;
    float t;        /* 0..1 through it */
    uint32_t frame; /* counts up, one a frame */
    const uint8_t *fb;           /* the running animation's frame */
    uint8_t *old;                /* the still */
    const uint16_t *palette;     /* the running animation's: RGB565, as fx_start gives it */
    const uint16_t *old_palette; /* the still's */
};

/* The animations that are screensavers, and the way into each
 * (FX_TR_NONE: it isn't one). The dongle keeps these out of the screens the
 * Screen keys step through. */
int fx_saver_count(void);
int fx_saver(int n);
int fx_saver_way_in(int fx);

/* Call when one starts, after the fx_start of the animation it goes to. */
void fx_transition_begin(int kind);

/* How hard the animation should run meanwhile (fx_input's energy), or a
 * negative number to leave that to the typing. */
float fx_transition_energy(int kind, float t);

/* Each frame, before fx_frame: what a way in does to the animation itself. */
void fx_transition_step(const struct fx_transition *tr);

/* Each frame, after fx_frame: row `y` of the picture to show, FX_W pixels
 * of RGB565. */
void fx_transition_row(const struct fx_transition *tr, int y, uint16_t *out);
