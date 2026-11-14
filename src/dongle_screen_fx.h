/* Prospector dongle screen animations: src/dongle_screen_fx.c */

#pragma once

/* Step through the screens (the module's status screen, then each
 * animation, and round again): 1 for the next, -1 for the one before. */
void dongle_screen_fx_step(int by);
