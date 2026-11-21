/*
 * The dongle's own typing speed (src/dongle_wpm.c), in place of ZMK's.
 */

#pragma once

/* A key has typed `c` (src/dongle_screen_fx.c's typed_char): a backspace
 * takes one off, anything else adds one. */
void dongle_wpm_typed(char c);
