/*
 * Battery levels and the output, drawn over the dongle's animations in the
 * fonts of the Prospector module's Field screen, the battery levels a size
 * up from there (the module builds all its fonts whichever layout is chosen;
 * the linker keeps the ones used here), at the panel's full 280x240 rather
 * than the animation's doubled pixels.
 *
 * Left battery bottom left, right battery bottom right, and between them
 * the wireless symbol with the Bluetooth profile number (nothing over USB).
 * The symbol takes the animation's colour when the computer is connected
 * and pulses grey when it isn't. Colours come from the animation's theme
 * (fx_theme), so the text can be dark where the picture is bright.
 *
 * The text is kept as three small masks, worked out only when something
 * changes, and mixed into the pixels on their way to the panel.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fx.h"

#define OVERLAY_ROWS 35
#define OVERLAY_BATTERY_W 75 /* "100" */
#define OVERLAY_OUTPUT_W 72
#define OVERLAY_BYTES ((2 * OVERLAY_BATTERY_W + OVERLAY_OUTPUT_W) * OVERLAY_ROWS)

/* On the panel: the rows the masks cover, and the output's columns. */
#define OVERLAY_TOP 185
#define OVERLAY_OUTPUT_X ((FX_W * 2 - OVERLAY_OUTPUT_W) / 2)

/* First and last frame row (FX_H of them) the masks touch. */
#define OVERLAY_FRAME_ROW0 (OVERLAY_TOP / 2)
#define OVERLAY_FRAME_ROW1 ((OVERLAY_TOP + OVERLAY_ROWS - 1) / 2)

/* How solid the pulsing symbol is at its strongest. */
#define OVERLAY_PULSE_MAX 16

/* OVERLAY_BYTES for the masks. Call before overlay_render. */
void overlay_set_memory(uint8_t *memory);

/* The colours to mix the masks in with: takes effect on the next blend. */
void overlay_set_theme(const struct fx_theme *theme);

/* Work the masks out again. Returns whether the symbol is one that pulses
 * (Bluetooth, and the computer isn't connected). */
bool overlay_render(const struct fx_status *status);

/* How solid the pulsing symbol is for a time in milliseconds: it breathes
 * once a second. 0..OVERLAY_PULSE_MAX, for overlay_set_pulse. */
int overlay_pulse_at(uint32_t ms);
void overlay_set_pulse(int level);

/* Mix the masks into `count` panel pixels starting at (x, y). The pixels are
 * RGB565 with the bytes swapped, as the panel takes them. */
void overlay_blend_row(uint16_t *pixels, int x, int y, int count);
