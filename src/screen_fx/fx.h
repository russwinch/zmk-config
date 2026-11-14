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

struct fx_input {
    float step;                /* time since the last frame, in 30ths of a second */
    float energy;              /* 0..1: how hard you are typing */
    uint8_t n_keys;            /* keys pressed since the last frame */
    uint8_t keys[FX_MAX_KEYS]; /* their key positions */
    uint8_t n_chars;           /* characters typed since the last frame */
    char chars[FX_MAX_CHARS];  /* printable ASCII, '\n' or '\b' */
};

struct fx_status {
    uint8_t battery[2];     /* left, right: percent, 0 = not known */
    bool half_connected[2]; /* left, right */
    bool host_connected;    /* the computer */
    bool usb;               /* typing over USB, not Bluetooth */
    bool host_open;         /* Bluetooth: nothing paired on this profile */
    uint8_t profile;        /* Bluetooth: profile number, from 0 */
    char host[4];           /* "USB", "BT1".. */
};

int fx_count(void);
const char *fx_name(int fx);

/* FX_W * FX_H bytes for the animations to keep between frames. Call before
 * fx_start, and again if the memory moves. */
void fx_set_canvas(uint8_t *memory);

/* Start animation `fx`: resets it, fills `palette` (256 x RGB565). */
void fx_start(int fx, uint16_t *palette);

/* Draw the next frame into `fb` (FX_W * FX_H palette indexes). */
void fx_frame(int fx, uint8_t *fb, const struct fx_input *in);

/* Battery and connection text over the frame (`status` NULL: leave it out,
 * the dongle draws its own, src/screen_fx/status_overlay.c); `banner` (or
 * NULL) top left. */
void fx_draw_status(uint8_t *fb, const struct fx_status *status, const char *banner);
