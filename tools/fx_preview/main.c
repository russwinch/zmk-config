/*
 * Runs the dongle's screen animations (src/screen_fx) on a computer and
 * writes the frames to stdout as raw RGB, 280x240 each, for fx_preview.py.
 *
 *   main <animation number> <frames> <frames per second>
 *
 * The typing is made up: idle, then speeding up to a fast burst, then idle
 * again, with a key press at the matching rate.
 */

#include <stdio.h>
#include <stdlib.h>

#include "fx.h"

int main(int argc, char **argv) {
    int fx = argc > 1 ? atoi(argv[1]) : 0;
    int frames = argc > 2 ? atoi(argv[2]) : 150;
    float fps = argc > 3 ? atof(argv[3]) : 15;

    static uint8_t fb[FX_W * FX_H], canvas[FX_W * FX_H];
    static uint8_t out[FX_W * 2 * 3];
    uint16_t palette[256];
    struct fx_status status = {{85, 12}, {true, true}, true, "USB"};

    fx_set_canvas(canvas);
    fx_start(fx, palette);

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
            energy += 0.03f;
        }
        float target = wpm / 60 > 1 ? 1 : wpm / 60;
        float rate = (target > energy ? 0.12f : 0.03f) * in.step;
        energy += (target - energy) * (rate > 1 ? 1 : rate);
        if (energy > 1) {
            energy = 1;
        }
        in.energy = energy;

        fx_frame(fx, fb, &in);
        fx_draw_status(fb, &status, f < fps * 2 ? fx_name(fx) : NULL);

        for (int y = 0; y < FX_H * 2; y++) {
            for (int x = 0; x < FX_W * 2; x++) {
                uint16_t c = palette[fb[(y / 2) * FX_W + x / 2]];
                out[x * 3] = (c >> 8) & 0xf8;
                out[x * 3 + 1] = (c >> 3) & 0xfc;
                out[x * 3 + 2] = (c << 3) & 0xf8;
            }
            fwrite(out, 1, sizeof(out), stdout);
        }
    }
    return 0;
}
