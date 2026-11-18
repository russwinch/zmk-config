/* Whether a half (0 left, 1 right) is on USB power, as the dongle last read
 * it (src/dongle_half_charging.c). False when not known. */
#pragma once

#include <stdbool.h>

bool dongle_half_charging(int half);
