/* What the dongle reads from each half (0 left, 1 right) beyond ZMK's own:
 * src/dongle_half_info.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// On USB power, as last read. False when not known.
bool dongle_half_charging(int half);

// Unplugged a moment ago, with no battery reading since: the level held is
// still the charger's, near 100.
bool dongle_half_settling(int half);

// The half's chip temperature in whole degrees C; false when not known.
bool dongle_half_temperature(int half, int8_t *celsius);

// The half's battery voltage in millivolts, as it last said (about once a
// minute, when it has moved); 0 when not known.
uint16_t dongle_half_millivolts(int half);

// The power the half's radio transmits at, in dBm, as its firmware was built;
// false when not known.
bool dongle_half_tx_power(int half, int8_t *dbm);
