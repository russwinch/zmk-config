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
    bool charging[2];       /* left, right: on USB power: a bolt beside its level, or over the animations
                             * in its place (the level reads high while charging: the charging voltage) */
    bool bolt_off[2];       /* left, right: charging, below 100, and the bolt in the off half of its flash */
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
#define FX_HISTORY 94      /* seconds of each link's signal and the typing speed kept */
#define FX_SIGNAL_DOWN (-128) /* in the signal history: a second the link was down */
#define FX_CONN_HISTORY 40  /* slots of each link's connection history kept */
#define FX_CONN_SLOT_S 60   /* seconds in each: 40 of them are 40 minutes */
#define FX_CONN_IDLE 1      /* in it: the keyboard idle the whole slot (the halves may be asleep) */
#define FX_CONN_USB (-1)    /* and for the computer over USB: ready to type (no radio to read) */
#define FX_MV_HISTORY 30    /* slots of the volts history kept */
#define FX_MV_SLOT_S 60     /* seconds in each: 30 of them are 30 minutes */

/* Two readings of a link (signal_log's or conn_log's) as one: down if
 * either was, else the weaker dBm, else idle, else 0 (not known). */
static inline int8_t fx_conn_worse(int8_t a, int8_t b) {
    if (a == FX_SIGNAL_DOWN || b == FX_SIGNAL_DOWN) {
        return FX_SIGNAL_DOWN;
    }
    if (b == 0 || b == FX_CONN_IDLE) {
        return a == 0 ? b : a;
    }
    if (a == 0 || a == FX_CONN_IDLE) {
        return b;
    }
    return a < b ? a : b;
}
#define FX_MAX_ENCODERS 4

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
    uint32_t crash_pc; /* FX_CRASH_FAULT: where (the program counter), 0 not known */
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
    uint32_t presses[2];  /* key presses since power-up: left, right */
    uint16_t bounce_ms;   /* shortest release-to-press of one key (0: none seen) */
    uint8_t bounce_key;
    uint32_t usb_reports; /* keyboard and media reports sent over USB since power-up */
    uint16_t usb_errors;  /* and those that failed (the computer not taking them) */
    char half_reads[2][3]; /* left, right: where the dongle has got with its charging state (see
                            * dongle_half_info_progress), "" when not connected */
    uint8_t half_err[2];   /* and the last error: ATT error code, or 0x80 | errno; 0 for none */
    uint16_t half_mv[2];   /* left, right: the battery's millivolts; 0 when not known */
    int32_t drain_hundredths_mw[2]; /* and its average draw, hundredths of a milliwatt; -1 until known */
    bool drain_rough[2];            /* and whether that may still be well out */
    bool estimating[2];             /* left, right: charging, or not yet settled since: shown in place of
                                     * ZMK's level (high under the charger), with a ~ */
    uint8_t battery_estimate[2];    /* and the estimated percent, 0 = not known */
    uint16_t battery_mah;           /* each half's battery, for how long it would last at that draw */
    /* each link's signal each second, kept whichever screen shows: dBm,
     * the newest last; left, right, the computer; 0 not known (and the
     * computer's while typing over USB), FX_SIGNAL_DOWN down */
    int8_t signal_log[3][FX_HISTORY];
    uint8_t wpm_log[FX_HISTORY]; /* and the typing speed, 255 at most */
    /* each link's connection over a longer time: the worst of each
     * FX_CONN_SLOT_S seconds, the newest last and still filling; as
     * signal_log, or FX_CONN_IDLE, or for the computer FX_CONN_USB over USB
     * (FX_SIGNAL_DOWN when USB isn't ready, idle when the computer sleeps) */
    int8_t conn_log[3][FX_CONN_HISTORY];
    /* the halves' battery and the dongle's supply over a longer time: the
     * millivolts at the end of each FX_MV_SLOT_S seconds, the newest last
     * and still filling; left, right, the dongle; 0 not known */
    uint16_t mv_log[3][FX_MV_HISTORY];
    uint16_t dongle_mv;    /* the dongle's 3.3 V supply, millivolts; 0 when not known */
    bool dongle_temp_known;
    int8_t dongle_temp_c;  /* the dongle's chip temperature */
    char dongle_mv_why[3], dongle_temp_why[3]; /* when not known, why: "WT" not read yet, "TO" a read
                                                * not back, "E1".. failed, "NB" not built in */
    struct fx_event log[FX_DEBUG_LOG]; /* the newest last */
    uint8_t n_log;
};

struct fx_input {
    float step;                /* time since the last frame, in 30ths of a second */
    float energy;              /* 0..1: how hard you are typing */
    uint16_t wpm;              /* words per minute: src/dongle_wpm.c */
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

/* In fx_glyph_rows (as "\x7f" in a string): a lightning bolt, shown for a
 * half on USB power (beside its level on the Keys and Debug screens, in its
 * place over the animations): flashing while under 100, solid at 100
 * (fx_status.bolt_off). */
#define FX_GLYPH_BOLT '\x7f'
#define FX_BOLT "\x7f"

/* Also in fx_glyph_rows: the Mac's modifier symbols, control, option,
 * shift and command. */
#define FX_GLYPH_CTRL '\x01'
#define FX_GLYPH_ALT '\x02'
#define FX_GLYPH_SHIFT '\x03'
#define FX_GLYPH_GUI '\x04'


/*
 * The keyboards' shapes, as tools/boards.py measured them (boards.h): each
 * key position where it is on the board, not where it is in the matrix. In
 * quarter millimetres, y down, x from the left half's inner edge (so
 * negative); a part on the right half is the mirror of that.
 */
enum { FX_BOARD_WATERFOWL, FX_BOARD_CORNE, FX_BOARD_URCHIN, FX_BOARD_FORAGER, FX_BOARDS };
enum { FX_PART_KEY, FX_PART_DIAL, FX_PART_ROLLER, FX_PART_SCREEN, FX_PART_CONTROLLER };
#define FX_PART_KIND 0x0f
#define FX_PART_RIGHT 0x10 /* a key on the right half */
#define FX_PART_BOTH 0x20  /* anything else: on both halves, not only the left */
struct fx_part {
    int16_t x, y; /* its middle */
    int8_t rot;   /* degrees clockwise */
    uint8_t w, h;
    uint8_t kind; /* FX_PART_* */
};
struct fx_board {
    const struct fx_part *keys; /* by key position */
    uint8_t n_keys;
    const struct fx_part *extras; /* the screen, the controller */
    uint8_t n_extras;
    const int16_t *outline; /* the left half's PCB: x, y, x, y.. */
    uint8_t n_outline;
    const int16_t *cover; /* and the cover over its controller, or NULL */
    uint8_t n_cover;
};
/* Which board the Keys screen draws (FX_BOARD_*); until set, the Urchin. */
void fx_set_board(int board);

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
    FX_TR_PULLBACK,   /* into Showroom: the camera pulls back from the picture, on the dongle's screen, to the board */
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
