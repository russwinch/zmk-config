/* Prospector dongle screen animations: src/dongle_screen_fx.c */

#pragma once

/* Show the next screen: the module's status screen, then each animation. */
void dongle_screen_fx_next(void);

/* Step `delta` screens on (or back), wrapping round. */
void dongle_screen_fx_step(int delta);
