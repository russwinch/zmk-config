/*
 * Status the Prospector dongle sends to the halves (Waterfowl, Aurora
 * Corne), so the left
 * half's nice!view can show the dongle's Bluetooth profile and the other
 * half's battery, and whether that half is on USB power.
 *
 * It rides on ZMK's normal "run this behavior on the peripheral" message:
 * the dongle invokes the behavior named HALF_STATUS_BEHAVIOR_NAME on each
 * half with the two 32-bit parameters below. The half defines that behavior
 * (node `hstatus` in config/half_status.dtsi) and stores the values.
 *
 * Sender:   src/dongle_half_status.c   (dongle)
 * Receiver: src/half_status_screen.c   (left half)
 */

#pragma once

#include <zephyr/sys/util.h>

// The behavior's devicetree node name; ZMK sends it as a string. Over
// Bluetooth split only 8 characters fit (ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN is 9,
// with the NUL): a longer name is cut short and the half can't find it.
#define HALF_STATUS_BEHAVIOR_NAME "hstatus"
#define HALF_STATUS_BEHAVIOR_NAME_MAX 8

// param1
#define HALF_STATUS_OTHER_BATTERY_MASK GENMASK(7, 0)  // other half's battery %, 0xFF = unknown
#define HALF_STATUS_PROFILE_MASK GENMASK(11, 8)       // active BLE profile index
#define HALF_STATUS_OUTPUT_USB BIT(12)                // dongle is typing over USB
#define HALF_STATUS_OTHER_CHARGING BIT(13)            // other half is on USB power (left 0 by
                                                      // dongles from before it: not charging)
#define HALF_STATUS_OTHER_BATTERY_UNKNOWN 0xFF

// param2
#define HALF_STATUS_CONNECTED_MASK GENMASK(4, 0)   // bit n: profile n connected
#define HALF_STATUS_BONDED_MASK GENMASK(9, 5)      // bit n: profile n paired
#define HALF_STATUS_VERSION_MASK GENMASK(31, 24)   // bumped if the layout changes
#define HALF_STATUS_VERSION 1

#define HALF_STATUS_PROFILE_COUNT 5

// The dongle: send the halves their status soon (src/dongle_half_status.c),
// e.g. when a half's charging state changes (src/dongle_half_info.c).
void dongle_half_status_send_soon(void);

// A left half's nice!view: draw it again soon, from the display thread
// (src/half_status_screen.c), e.g. when the right half's charging state
// changes.
void half_status_screen_redraw(void);

// A half: tell the dongle its charging state again (src/half_charging.c),
// e.g. once the battery has been read after unplugging (src/battery_refresh.c).
void half_charging_update(void);
