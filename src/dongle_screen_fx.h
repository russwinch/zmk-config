/* Prospector dongle screen animations: src/dongle_screen_fx.c */

#pragma once

/* Step through the screens (the module's status screen, then each
 * animation, and round again): 1 for the next, -1 for the one before. */
void dongle_screen_fx_step(int by);

/* Step through the colour themes of the Keys, Debug, Tracker and Terminal
 * screens (src/screen_fx/colors.h), and round again: 1 for the next, -1 for the one
 * before. The theme's name shows for a moment on whichever screen is up. */
void dongle_screen_fx_theme_step(int by);
