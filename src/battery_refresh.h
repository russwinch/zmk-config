#pragma once

#include <stdbool.h>

// Whether the screens should show this half as on USB power (a bolt rather
// than its level): while it is, and after unplugging until the battery has
// been read again, so the charger's near-100 reading is never shown as the
// level (src/battery_refresh.c). Without that file, just whether it is.
bool battery_refresh_shows_usb_power(void);
