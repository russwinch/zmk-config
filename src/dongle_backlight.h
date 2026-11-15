/*
 * The Prospector dongle's screen backlight (src/dongle_idle_backlight.c),
 * for the animated screens (src/dongle_screen_fx.c).
 */

#pragma once

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_WATERFOWL_DONGLE_IDLE_BACKLIGHT)
/* The first picture is ready: bring the backlight up, which has stayed dark
 * since power-up for it. Any thread. */
void dongle_backlight_on(void);
#else
static inline void dongle_backlight_on(void) {}
#endif
