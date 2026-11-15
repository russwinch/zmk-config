/*
 * Battery levels and the output, drawn over the dongle's animations in the
 * fonts of the Prospector module's Field screen, the battery levels a size
 * up from there (the module builds all its fonts whichever layout is chosen;
 * the linker keeps the ones used here), at the panel's full 280x240 rather
 * than the animation's doubled pixels.
 *
 * Left battery bottom left, right battery bottom right. A level is red at
 * FX_LOW_BATTERY or under, and flashes at FX_FLAT_BATTERY or under.
 *
 * In the bottom middle, the wireless symbol, with the Bluetooth profile
 * number to its right:
 *
 *   on Bluetooth, connected      the symbol in the animation's colour. The
 *                                number shows for two seconds after the
 *                                profile is changed or the computer
 *                                connects, then fades away
 *   on Bluetooth, not connected  the symbol pulsing grey, and the number
 *   over USB                     nothing, but for two seconds after the
 *                                profile is changed: then both, and both
 *                                fade
 *
 * Colours come from the animation's theme (fx_theme), so the text can be
 * dark where the picture is bright. The theme can also have the numbers in
 * the animations' own 5x7 letters, smaller, and move everything nearer the
 * bottom of the screen. The screensavers' themes leave all of it out but
 * for a dot where the level of a half whose battery is low would be, which
 * flashes as the level would.
 *
 * The text is kept as four small masks, worked out only when something
 * changes, and mixed into the pixels on their way to the panel.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fx.h"

#define OVERLAY_ROWS 35
#define OVERLAY_BATTERY_W 75 /* "100" */
#define OVERLAY_SYMBOL_W 40
#define OVERLAY_NUMBER_W 26
#define OVERLAY_BYTES ((2 * OVERLAY_BATTERY_W + OVERLAY_SYMBOL_W + OVERLAY_NUMBER_W) * OVERLAY_ROWS)

/* The masks. */
enum { OVERLAY_LEFT, OVERLAY_RIGHT, OVERLAY_SYMBOL, OVERLAY_NUMBER, OVERLAY_AREAS };

/* On the panel: the first row the masks cover, before the theme's drop. */
#define OVERLAY_TOP 185

/* First and last frame row (FX_H of them) the masks touch, with the theme
 * that is set. */
int overlay_frame_row0(void);
int overlay_frame_row1(void);

/* The panel columns a mask covers, with the theme that is set. */
void overlay_area_span(int area, int *x, int *width);

/* OVERLAY_BYTES for the masks. Call before overlay_update. */
void overlay_set_memory(uint8_t *memory);

/* The colours, letters and place: all take effect at the next
 * overlay_update. */
void overlay_set_theme(const struct fx_theme *theme);

/* Bring the text up to date with `status` at a time in milliseconds: every
 * frame, since what pulses, flashes and fades goes by the time. Returns a
 * bit for each mask (1 << OVERLAY_LEFT...) that looks different from the
 * last call, for the caller to send those parts of the picture again. */
unsigned overlay_update(const struct fx_status *status, uint32_t ms);

/* Mix the masks into `count` panel pixels starting at (x, y). The pixels are
 * RGB565 with the bytes swapped, as the panel takes them. */
void overlay_blend_row(uint16_t *pixels, int x, int y, int count);
