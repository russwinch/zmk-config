/* What the dongle reads from each half (0 left, 1 right) beyond ZMK's own:
 * src/dongle_half_info.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// On USB power, as last read. False when not known.
bool dongle_half_charging(int half);

// The half's chip temperature in whole degrees C; false when not known.
bool dongle_half_temperature(int half, int8_t *celsius);
