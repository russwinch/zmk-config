/*
 * Runs the dongle's screen animations (src/screen_fx) on a computer and
 * writes the frames to stdout as raw RGB, 280x240 each, for fx_preview.py.
 *
 *   main <animation number> <frames> <frames per second>
 *
 * The typing is made up: idle, then speeding up to a fast burst, then idle
 * again, with a key press at the matching rate, each typing the next
 * character of `typed`.
 *
 * So is the status: the left half full and the right one nearly flat, on
 * Bluetooth profile 1 with the computer connected, then (from 55% of the way
 * through) not connected, then (from 85%) over USB.
 *
 *   main saver <animation showing> <screensaver animation> <frames per second>
 *
 * The screensaver coming over an animation and going again, as the dongle
 * does it (fx.h): 2 seconds of the animation, the way in, 4 seconds of the
 * screensaver, then a key and typing for the whole of the way out, and 3
 * seconds of the animation back. Without the status text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "status_overlay.h"

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

int main(int argc, char **argv) {
    if (argc > 3 && strcmp(argv[1], "saver") == 0) {
        return saver_main(atoi(argv[2]), atoi(argv[3]), argc > 4 ? atof(argv[4]) : 15);
    }
    int fx = argc > 1 ? atoi(argv[1]) : 0;
    int frames = argc > 2 ? atoi(argv[2]) : 150;
    float fps = argc > 3 ? atof(argv[3]) : 15;

    static uint8_t fb[FX_W * FX_H], canvas[FX_W * FX_H];
    static uint8_t out[FX_W * 2 * 3], masks[OVERLAY_BYTES];
    static uint16_t row[FX_W * 2];
    uint16_t palette[256];
    struct fx_status status = {.battery = {100, 12}, .half_connected = {true, true}, .profile = 1};
    int stage = -1;
    /* made-up readings for the Debug screen */
    static struct fx_debug debug = {
        .uptime_s = 4365, .reset_cause = 2, .fps = 15, .frame_ms = 38, .heap_known = true, .heap_used = 62,
        .heap_peak = 71,
        .half = {{.up = true, .rssi_known = true, .rssi = -52, .interval = 6, .latency = 30, .drops = 2, .reason = 0x08},
                 {.up = true, .rssi_known = true, .rssi = -71, .interval = 6, .latency = 30}},
        .host = {.up = true, .rssi_known = true, .rssi = -48, .interval = 12, .drops = 1, .reason = 0x13},
        .battery_age_s = {12, 340}, .profile_state = {1, 2, 0, 1, 0}, .usb = 1, .layer = 3, .layer_name = "Nav",
        .layers = 0x9, .bounce_ms = 18, .bounce_key = 23, .keyboard = "Forager",
    };

    fx_set_canvas(canvas);
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
            debug.mods = debug.presses % 7 == 0 ? 0x02 : 0;
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

        const int now = at < 0.55f ? 0 : at < 0.85f ? 1 : 2;
        if (now != stage) {
            stage = now;
            status.host_connected = stage != 1;
            status.usb = stage == 2;
            overlay_render(&status);
        }
        overlay_set_pulse(overlay_pulse_at((uint32_t)(f * 1000 / fps)));

        in.status = &status;
        debug.uptime_s = 4365 + (uint32_t)(f / fps);
        debug.wpm = (uint16_t)wpm;
        debug.host.up = status.host_connected && !status.usb;
        debug.usb = status.usb ? 2 : 1;
        in.debug = &debug;
        fx_frame(fx, fb, &in);
        const bool plain = fx_is_debug(fx);
        fx_draw_banner(fb, f < fps * 2 && !plain && !fx_shows_keys(fx) ? fx_name(fx) : NULL);

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
