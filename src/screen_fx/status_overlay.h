/*
 * Battery levels and the output, drawn over the dongle's animations the way
 * the Prospector module's Field screen draws them: the same fonts (they are
 * in the firmware already), sizes and colours, at the panel's full 280x240
 * rather than the animation's doubled pixels.
 *
 * The text is kept as two small masks (bottom left, bottom right), worked
 * out only when something changes, and mixed into the pixels on their way
 * to the panel.
 */

#pragma once

#include <stdint.h>

#include "fx.h"

#define OVERLAY_ROWS 34
#define OVERLAY_BATTERY_W 140 /* "100/100" */
#define OVERLAY_OUTPUT_W 72
#define OVERLAY_BYTES ((OVERLAY_BATTERY_W + OVERLAY_OUTPUT_W) * OVERLAY_ROWS)

/* First and last frame row (FX_H of them) the masks touch. */
#define OVERLAY_TOP 188
#define OVERLAY_FRAME_ROW0 (OVERLAY_TOP / 2)
#define OVERLAY_FRAME_ROW1 ((OVERLAY_TOP + OVERLAY_ROWS - 1) / 2)

/* OVERLAY_BYTES for the masks. Call before overlay_render. */
void overlay_set_memory(uint8_t *memory);

/* Work the masks out again. */
void overlay_render(const struct fx_status *status);

/* Mix the masks into `count` panel pixels starting at (x, y). The pixels are
 * RGB565 with the bytes swapped, as the panel takes them. */
void overlay_blend_row(uint16_t *pixels, int x, int y, int count);
