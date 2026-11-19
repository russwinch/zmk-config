/*
 * Runs the dongle's screen animations (src/screen_fx) on a computer and
 * writes the frames to stdout as raw RGB, 280x240 each, for fx_preview.py.
 *
 *   main <animation number> <frames> <frames per second> [<theme number> [name]]
 *
 * In that colour theme (colors.h's schemes, from 0; it only shows on the screens
 * drawn in the theme's colours), and with a last argument, with the theme's
 * name over the picture as the dongle shows it when the theme is turned to.
 *
 * The typing is made up: idle, then speeding up to a fast burst, then idle
 * again, with a key press at the matching rate, each typing the next
 * character of `typed`. One key (the left hand's home row) is held down
 * from the start for seven seconds.
 *
 * So is the status: the left half full and the right one flat, on Bluetooth
 * profile 1 with the computer connected, profile 2 from 30% of the way
 * through, then (from 55%) not connected, then (from 80%) over USB, and
 * there profile 3 from 88%.
 *
 *   main saver <animation showing> <screensaver animation> <frames per second>
 *
 * The screensaver coming over an animation and going again, as the dongle
 * does it (fx.h): 2 seconds of the animation, the way in, 4 seconds of the
 * screensaver, then a key and typing for the whole of the way out, and 3
 * seconds of the animation back. Without the status text.
 *
 *   main power <animation number> <frames per second>
 *
 * The panel going off and coming back on (FX_TR_CRT_OFF, FX_TR_STATIC): 2
 * seconds of the animation, off, half a second of nothing, on, 2 seconds
 * more. The backlight's own fading isn't shown.
 *
 * FX_PREVIEW_BOARD=waterfowl or corne in the environment draws that board
 * (its keys, its encoders as knobs, its name) instead of the Forager, with
 * its encoders turned and pressed now and then.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "status_overlay.h"

/* The boards FX_PREVIEW_BOARD picks, as their dongles' overlays and
 * src/dongle_screen_fx.c's encoder_keys have them. */
#define RC(row, col) ((row) << 8 | (col))
static const uint16_t waterfowl_keys[] = {
    RC(0, 0), RC(0, 1), RC(0, 2), RC(0, 3), RC(0, 4), RC(0, 5), RC(0, 6), RC(0, 7), RC(0, 8), RC(0, 9),
    RC(1, 0), RC(1, 1), RC(1, 2), RC(1, 3), RC(1, 4), RC(1, 5), RC(1, 6), RC(1, 7), RC(1, 8), RC(1, 9),
    RC(2, 0), RC(2, 1), RC(2, 2), RC(2, 3), RC(2, 4), RC(2, 5), RC(2, 6), RC(2, 7), RC(2, 8), RC(2, 9),
    RC(3, 0), RC(3, 1), RC(3, 2), RC(3, 3), RC(3, 4), RC(3, 5), RC(3, 6), RC(3, 7), RC(3, 8), RC(3, 9),
};
static const int8_t waterfowl_encoders[] = {30, 34, 35, 39};
static const uint16_t corne_keys[] = {
    RC(0, 0), RC(0, 1), RC(0, 2), RC(0, 3), RC(0, 4), RC(0, 5), RC(0, 6), RC(0, 7), RC(0, 8), RC(0, 9), RC(0, 10), RC(0, 11),
    RC(1, 0), RC(1, 1), RC(1, 2), RC(1, 3), RC(1, 4), RC(1, 5), RC(1, 6), RC(1, 7), RC(1, 8), RC(1, 9), RC(1, 10), RC(1, 11),
    RC(2, 0), RC(2, 1), RC(2, 2), RC(2, 3), RC(2, 4), RC(2, 5), RC(2, 6), RC(2, 7), RC(2, 8), RC(2, 9), RC(2, 10), RC(2, 11),
    RC(3, 3), RC(3, 4), RC(3, 5), RC(3, 6), RC(3, 7), RC(3, 8),
};
static const int8_t corne_encoders[] = {36, 41};
#undef RC

/* The board's name, and its encoders' push positions (NULL: none). */
static const char *preview_board(const int8_t **encoders, int *n_encoders) {
    const char *board = getenv("FX_PREVIEW_BOARD");
    *encoders = NULL;
    *n_encoders = 0;
    if (board && strcmp(board, "waterfowl") == 0) {
        fx_set_keys(waterfowl_keys, sizeof(waterfowl_keys) / sizeof(waterfowl_keys[0]));
        *encoders = waterfowl_encoders;
        *n_encoders = sizeof(waterfowl_encoders);
        return "Waterfowl";
    }
    if (board && strcmp(board, "corne") == 0) {
        fx_set_keys(corne_keys, sizeof(corne_keys) / sizeof(corne_keys[0]));
        *encoders = corne_encoders;
        *n_encoders = sizeof(corne_encoders);
        return "Aurora Corne";
    }
    return "Forager";
}

static uint8_t saver_fb[FX_W * FX_H], saver_canvas[FX_W * FX_H], saver_old[FX_W * FX_H];
static uint16_t saver_palette[256], saver_old_palette[256];
static float saver_fps;
static unsigned saver_seed = 7;
static bool saver_unseen;

/* One frame of `fx`, with a key every `key_every` frames (0: none), shown
 * plain or (kind != FX_TR_NONE) through a transition at `t`. */
static void saver_frame(int fx, float energy, int key_every, int f, int kind, float t) {
    struct fx_status status = {.battery = {100, 60}, .half_connected = {true, true}, .host_connected = true};
    struct fx_input in = {.step = 30.0f / saver_fps, .energy = energy, .status = &status};
    struct fx_transition tr = {.kind = (uint8_t)kind, .t = t, .frame = (uint32_t)f, .fb = saver_fb, .old = saver_old,
                               .palette = saver_palette, .old_palette = saver_old_palette};
    static uint16_t row[FX_W];
    static uint8_t out[FX_W * 2 * 3];

    if (key_every && f % key_every == 0) {
        saver_seed = saver_seed * 1103515245 + 12345;
        in.keys[in.n_keys++] = (saver_seed >> 16) % 34;
    }
    const float wanted = fx_transition_energy(kind, t);
    if (wanted >= 0) {
        in.energy = wanted;
    }
    fx_transition_step(&tr);
    fx_frame(fx, saver_fb, &in);
    for (int y = 0; y < FX_H && !saver_unseen; y++) {
        fx_transition_row(&tr, y, row);
        for (int x = 0; x < FX_W * 2; x++) {
            out[x * 3] = (row[x / 2] >> 8) & 0xf8;
            out[x * 3 + 1] = (row[x / 2] >> 3) & 0xfc;
            out[x * 3 + 2] = (row[x / 2] << 3) & 0xf8;
        }
        fwrite(out, 1, sizeof(out), stdout);
        fwrite(out, 1, sizeof(out), stdout);
    }
}

static int saver_main(int base, int saver, float fps) {
    const int in_frames = (int)(FX_SAVER_IN_MS * fps / 1000), out_frames = (int)(FX_SAVER_OUT_MS * fps / 1000),
              cut_frames = (int)(FX_SAVER_CUT_MS * fps / 1000);
    saver_fps = fps;
    fx_set_canvas(saver_canvas);
    fx_start(base, saver_palette);
    for (int f = 0; f < 6 * fps; f++) { /* 4 seconds of it unseen, typing, so there is a picture */
        saver_unseen = f < 4 * fps;
        saver_frame(base, saver_unseen ? 0.5f : 0.1f, saver_unseen ? 3 : 0, f, FX_TR_NONE, 0);
    }

    memcpy(saver_old, saver_fb, sizeof(saver_old));
    memcpy(saver_old_palette, saver_palette, sizeof(saver_old_palette));
    fx_start(saver, saver_palette);
    fx_transition_begin(fx_saver_way_in(saver));
    float energy = 0;
    for (int f = 0; f < in_frames; f++) {
        energy = fx_transition_energy(fx_saver_way_in(saver), (float)f / (in_frames - 1));
        saver_frame(saver, 0, 0, f, fx_saver_way_in(saver), (float)f / (in_frames - 1));
    }
    energy = energy < 0 ? 0 : energy;
    for (int f = 0; f < 4 * fps; f++) {
        energy *= 0.94f;
        saver_frame(saver, energy, 0, f, FX_TR_NONE, 0);
    }

    fx_transition_begin(FX_TR_GLITCH);
    for (int f = 0; f < out_frames; f++) { /* a key, and typing */
        energy += (1 - energy) * 0.1f;
        saver_frame(saver, energy, 2, f, FX_TR_GLITCH, (float)f / (out_frames - 1));
    }
    memcpy(saver_old, saver_fb, sizeof(saver_old));
    memcpy(saver_old_palette, saver_palette, sizeof(saver_old_palette));
    fx_start(base, saver_palette);
    fx_transition_begin(FX_TR_CUT);
    for (int f = 0; f < cut_frames + 3 * fps; f++) {
        saver_frame(base, energy, 2, f, f < cut_frames ? FX_TR_CUT : FX_TR_NONE, (float)f / (cut_frames - 1));
    }
    return 0;
}

static int power_main(int fx, float fps) {
    const int still = (int)(2 * fps), off = (int)(FX_POWER_OFF_MS * fps / 1000), dark = (int)(fps / 2),
              on = (int)(FX_POWER_ON_MS * fps / 1000);
    saver_fps = fps;
    fx_set_canvas(saver_canvas);
    fx_start(fx, saver_palette);
    for (int f = 0; f < 4 * fps; f++) { /* unseen, typing, so there is a picture */
        saver_unseen = true;
        saver_frame(fx, 0.5f, 3, f, FX_TR_NONE, 0);
    }
    saver_unseen = false;
    for (int f = 0; f < still; f++) {
        saver_frame(fx, 0.3f, 4, f, FX_TR_NONE, 0);
    }
    for (int f = 0; f < off + dark; f++) {
        saver_frame(fx, 0.1f, 0, f, FX_TR_CRT_OFF, f < off ? (float)f / (off - 1) : 1);
    }
    for (int f = 0; f < on; f++) {
        saver_frame(fx, 0.3f, 4, f, FX_TR_STATIC, (float)f / (on - 1));
    }
    for (int f = 0; f < still; f++) {
        saver_frame(fx, 0.3f, 4, f, FX_TR_NONE, 0);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 3 && strcmp(argv[1], "saver") == 0) {
        return saver_main(atoi(argv[2]), atoi(argv[3]), argc > 4 ? atof(argv[4]) : 15);
    }
    if (argc > 2 && strcmp(argv[1], "power") == 0) {
        return power_main(atoi(argv[2]), argc > 3 ? atof(argv[3]) : 15);
    }
    int fx = argc > 1 ? atoi(argv[1]) : 0;
    int frames = argc > 2 ? atoi(argv[2]) : 150;
    float fps = argc > 3 ? atof(argv[3]) : 15;
    const int theme = argc > 4 ? atoi(argv[4]) : 0;
    const bool named = argc > 5;
    char place[16];
    snprintf(place, sizeof(place), "%d/%d", theme + 1, fx_scheme_count());

    static uint8_t fb[FX_W * FX_H], canvas[FX_W * FX_H];
    static uint8_t out[FX_W * 2 * 3], masks[OVERLAY_BYTES];
    static uint16_t row[FX_W * 2];
    uint16_t palette[256];
    struct fx_status status = {.battery = {100, 4}, .half_connected = {true, true}, .profile = 1};
    /* made-up readings for the Debug screen */
    static struct fx_debug debug = {
        .firmware = "B149", .uptime_s = 4365, .reset_cause = 2, .fps = 15, .frame_ms = 38, .heap_known = true, .heap_used = 62,
        .heap_peak = 71,
        .half = {{.up = true, .rssi_known = true, .rssi = -52, .drops = 2, .reason = 0x08},
                 {.up = true, .rssi_known = true, .rssi = -71}},
        .host = {.up = true, .rssi_known = true, .rssi = -48, .drops = 1, .reason = 0x13},
        .usb_reports = 18342, .usb_errors = 0, .drain_tenths = {6, 13}, .mv = {3921, 3312, 3604},
        .tx_known = {true, true, false}, .tx_dbm = {8, 0},
        .temp_known = {true, true, true}, .temp_c = {27, 31, 26}, .profile_state = {1, 2, 0, 1, 0}, .usb = 1, .layer = 3, .layer_name = "Nav",
        .layers = 0x9, .bounce_ms = 18, .bounce_key = 23,
    };
    const int8_t *encoders;
    int n_encoders;
    debug.keyboard = preview_board(&encoders, &n_encoders);
    fx_set_encoders(encoders, n_encoders);

    fx_set_canvas(canvas);
    fx_set_scheme(theme);
    fx_start(fx, palette);
    overlay_set_memory(masks);
    overlay_set_theme(fx_theme(fx));

    static const char typed[] = "git status\ncd zmk-config && ls -la\nwest build -b xiao_ble\b\b\b\bble//zmk\n"
                                "echo \"Hello, World!\" | wc -c\nthe quick brown fox jumps over the lazy dog\n";
    unsigned typed_at = 0;
    float energy = 0, key_debt = 0;
    unsigned key_seed = 7;
    for (int f = 0; f < frames; f++) {
        float at = (float)f / frames;
        /* words per minute: 0 for the first fifth, up to 90, 0 for the last quarter */
        float wpm = at < 0.2f ? 0 : at < 0.75f ? 90 * (at - 0.2f) / 0.35f : 0;
        if (wpm > 90) {
            wpm = 90;
        }
        struct fx_input in = {.step = 30.0f / fps};

        key_debt += wpm * 5 / 60 / fps; /* 5 key presses to a word */
        while (key_debt >= 1 && in.n_keys < FX_MAX_KEYS) {
            key_debt -= 1;
            key_seed = key_seed * 1103515245 + 12345;
            in.keys[in.n_keys++] = (key_seed >> 16) % 34;
            /* the Debug screen's log: the press, then the keycode it sent */
            const struct fx_event events[2] = {
                {.kind = 'P', .down = true, .source = in.keys[in.n_keys - 1] % 10 >= 5, .a = in.keys[in.n_keys - 1],
                 .gap_ms = (uint16_t)(12000 / (wpm + 1))},
                {.kind = 'K', .down = true, .a = 7, .b = 4 + in.keys[in.n_keys - 1] % 26, .gap_ms = 1},
            };
            debug.log[0] = debug.log[2];
            debug.log[1] = events[0];
            debug.log[2] = events[1];
            debug.n_log = 3;
            debug.presses++;
            /* with a thumb key holding a layer and a home row key a modifier, now and then */
            debug.held_layer = debug.presses % 5 < 3 ? 1ull << 31 : 0;
            debug.held_mod = debug.presses % 7 < 2 ? 1ull << 16 : 0;
            debug.held = 1ull << in.keys[in.n_keys - 1] | debug.held_layer | debug.held_mod;
            debug.layer = debug.held_layer ? 3 : 0;
            debug.layer_name = debug.held_layer ? "Nav" : "Mac";
            debug.mods = debug.presses % 7 == 0 ? 0x0a : 0;
            in.chars[in.n_chars++] = typed[typed_at++ % (sizeof(typed) - 1)];
            energy += 0.03f;
        }
        float target = wpm / 60 > 1 ? 1 : wpm / 60;
        float rate = (target > energy ? 0.12f : 0.03f) * in.step;
        energy += (target - energy) * (rate > 1 ? 1 : rate);
        if (energy > 1) {
            energy = 1;
        }
        in.energy = energy;
        in.held = f >= fps * 0.3f && f < fps * 7.3f ? 1ull << 11 : 0;
        /* the encoders: each turned one way then the other in its own stretch,
         * a step every few frames, then pressed for a moment */
        for (int e = 0; e < n_encoders; e++) {
            const float from = 0.05f + e * 0.2f, t = (at - from) / 0.15f;
            if (t >= 0 && t < 1 && f % 3 == 0) {
                /* as ZMK's EC11 driver gives it: whole degrees, 4 a pulse on the
                 * Waterfowl (80 steps a turn); here a click, 4 pulses, at a time */
                const int step = t < 0.6f ? 16 : -16;
                debug.turned[e] += step;
                debug.log[0] = debug.log[1];
                debug.log[1] = debug.log[2];
                debug.log[2] = (struct fx_event){.kind = 'E', .source = 2, .a = e, .b = (uint16_t)step, .gap_ms = 200};
                debug.n_log = 3;
            }
            if (t >= 1 && t < 1.2f) {
                debug.held |= 1ull << encoders[e];
            } else if (t >= 1.2f && t < 1.25f) {
                debug.held &= ~(1ull << encoders[e]);
            }
        }
        in.wpm = (uint16_t)wpm;

        status.host_connected = at < 0.55f || at >= 0.8f;
        status.usb = at >= 0.8f;
        status.charging[0] = at >= 0.8f; /* the left half plugged in too: a bolt for its level */
        status.profile_connected = at >= 0.8f && at < 0.88f;
        status.profile = at < 0.3f ? 1 : at < 0.88f ? 2 : 3;
        const bool plain = fx_is_debug(fx) || fx_shows_keys(fx);
        if (!plain) {
            overlay_update(&status, (uint32_t)(f * 1000 / fps));
        }

        in.status = &status;
        debug.uptime_s = 4365 + (uint32_t)(f / fps);
        debug.wpm = (uint16_t)wpm;
        debug.host.up = status.host_connected && !status.usb;
        debug.usb = status.usb ? 2 : 1;
        debug.usb_asleep = at >= 0.94f;
        /* readings that wander, as the real ones do */
        key_seed = key_seed * 1103515245 + 12345;
        debug.frame_ms = (uint8_t)(36 + (key_seed >> 16) % 5);
        if (f % (int)fps == 0) {
            debug.half[0].rssi = (int8_t)(-52 + (int)((key_seed >> 8) % 7) - 3);
            debug.half[1].rssi = (int8_t)(-71 + (int)((key_seed >> 12) % 13) - 6);
            debug.host.rssi = (int8_t)(-48 + (int)((key_seed >> 20) % 5) - 2);
        }
        in.debug = &debug;
        fx_frame(fx, fb, &in);
        if (named) {
            fx_draw_notice(fb, fx_scheme_name(theme), place);
        }

        for (int y = 0; y < FX_H * 2; y++) {
            /* as the dongle does: the panel's byte order, then the text */
            for (int x = 0; x < FX_W * 2; x++) {
                uint16_t c = palette[fb[(y / 2) * FX_W + x / 2]];
                row[x] = (uint16_t)(c >> 8 | c << 8);
            }
            if (!plain) {
                overlay_blend_row(row, 0, y, FX_W * 2);
            }
            for (int x = 0; x < FX_W * 2; x++) {
                uint16_t c = (uint16_t)(row[x] >> 8 | row[x] << 8);
                out[x * 3] = (c >> 8) & 0xf8;
                out[x * 3 + 1] = (c >> 3) & 0xfc;
                out[x * 3 + 2] = (c << 3) & 0xf8;
            }
            fwrite(out, 1, sizeof(out), stdout);
        }
    }
    return 0;
}
