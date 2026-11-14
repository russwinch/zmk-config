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

/* Palette entries 0..251 belong to the animation; these are fixed. */
#define FX_COLORS 252
#define FX_BLACK 252
#define FX_RED 253
#define FX_GREY 254
#define FX_WHITE 255

#define FX_MAX_KEYS 8
#define FX_MAX_CHARS 8

struct fx_status {
    uint8_t battery[2];     /* left, right: percent, 0 = not known */
    bool half_connected[2]; /* left, right */
    bool host_connected;    /* the computer */
    bool usb;               /* typing over USB, not Bluetooth */
    bool host_open;         /* Bluetooth: nothing paired on this profile */
    uint8_t profile;        /* Bluetooth: profile number, from 0 */
};

/* Colours (RGB565) for the status text drawn over an animation
 * (status_overlay.c), chosen to suit its picture. */
struct fx_theme {
    uint16_t ink;    /* the battery levels, and the Bluetooth profile number */
    uint16_t dim;    /* the profile number when the computer isn't connected */
    uint16_t accent; /* the wireless symbol when it is */
    uint16_t search; /* the symbol when it isn't: this one pulses */
    uint16_t low;    /* a battery that is nearly flat */
    uint16_t halo;   /* around all of it: dark for light text, light for dark */
};

/* --- what the Debug screen shows -------------------------------------------- */

#define FX_DEBUG_LOG 3
#define FX_DEBUG_PROFILES 5

/* A Bluetooth link: to a half, or to the computer. */
struct fx_link {
    bool up;
    bool rssi_known;
    int8_t rssi;       /* dBm, as the dongle hears the other end */
    uint16_t interval; /* connection interval, in units of 1.25 ms */
    uint16_t latency;  /* intervals the other end may skip */
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
    uint32_t uptime_s;
    uint32_t reset_cause; /* Zephyr's RESET_* bits */
    uint8_t activity;     /* 0 active, 1 idle, 2 asleep */
    uint8_t fps, frame_ms; /* this screen: frames a second, time to draw and send one */
    bool heap_known;
    uint8_t heap_used, heap_peak; /* LVGL's memory, percent */
    struct fx_link half[2], host;
    uint16_t battery_age_s[2]; /* since each half last reported its level */
    uint8_t profile_state[FX_DEBUG_PROFILES]; /* 0 open, 1 paired, 2 connected */
    uint8_t usb;          /* 0 no, 1 powered, 2 ready to type */
    uint8_t layer;        /* the highest one active */
    const char *layer_name; /* or NULL */
    uint32_t layers;      /* a bit for each active layer */
    uint8_t mods;         /* held: HID modifier bits */
    uint16_t wpm;
    uint64_t held;        /* a bit for each key position held */
    uint32_t presses;     /* key presses since power-up */
    uint16_t bounce_ms;   /* shortest release-to-press of one key (0: none seen) */
    uint8_t bounce_key;
    struct fx_event log[FX_DEBUG_LOG]; /* the newest last */
    uint8_t n_log;
};

struct fx_input {
    float step;                /* time since the last frame, in 30ths of a second */
    float energy;              /* 0..1: how hard you are typing */
    uint8_t n_keys;            /* keys pressed since the last frame */
    uint8_t keys[FX_MAX_KEYS]; /* their key positions */
    uint8_t n_chars;           /* characters typed since the last frame */
    char chars[FX_MAX_CHARS];  /* printable ASCII, '\n' or '\b' */
    const struct fx_status *status; /* for an animation that shows some of it itself */
    const struct fx_debug *debug;   /* for the Debug screen (fx_is_debug) */
};

int fx_count(void);
const char *fx_name(int fx);

const struct fx_theme *fx_theme(int fx);

/* The Debug screen: wants fx_input's debug filled in, fills the whole
 * picture with its own text, and takes no status text or banner over it. */
bool fx_is_debug(int fx);

/* FX_W * FX_H bytes for the animations to keep between frames. Call before
 * fx_start, and again if the memory moves. */
void fx_set_canvas(uint8_t *memory);

/* Start animation `fx`: resets it, fills `palette` (256 x RGB565). */
void fx_start(int fx, uint16_t *palette);

/* Draw the next frame into `fb` (FX_W * FX_H palette indexes). */
void fx_frame(int fx, uint8_t *fb, const struct fx_input *in);

/* The animation's name over the frame, top left, in the small letters. The
 * batteries and output are drawn over it afterwards, at the panel's own
 * size: src/screen_fx/status_overlay.c. */
void fx_draw_banner(uint8_t *fb, const char *banner);
