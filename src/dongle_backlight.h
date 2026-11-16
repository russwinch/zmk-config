/*
 * The Prospector dongle's screen backlight (src/dongle_idle_backlight.c),
 * for the animated screens (src/dongle_screen_fx.c).
 */

#pragma once

#include <zephyr/kernel.h>

/* How bright the backlight is when it is on. */
enum { DONGLE_BACKLIGHT_LOW, DONGLE_BACKLIGHT_MEDIUM, DONGLE_BACKLIGHT_HIGH, DONGLE_BACKLIGHT_LEVELS };

#if IS_ENABLED(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT)
/* The first picture is ready: bring the backlight up, which has stayed dark
 * since power-up for it. Any thread. */
void dongle_backlight_on(void);

/* The level: medium until another is set. Setting it fades the backlight
 * there if it is on; stepping goes low, medium, high and round again, and
 * returns the new level. Any thread. */
int dongle_backlight_level(void);
void dongle_backlight_set_level(int level);
int dongle_backlight_step(void);
#else
static inline void dongle_backlight_on(void) {}
static inline int dongle_backlight_level(void) { return DONGLE_BACKLIGHT_MEDIUM; }
static inline void dongle_backlight_set_level(int level) {}
static inline int dongle_backlight_step(void) { return DONGLE_BACKLIGHT_MEDIUM; }
#endif
