/*
 * Status the Prospector dongle sends to the Waterfowl halves, so the left
 * half's nice!view can show the dongle's Bluetooth profile and the other
 * half's battery.
 *
 * It rides on ZMK's normal "run this behavior on the peripheral" message:
 * the dongle invokes the behavior named HALF_STATUS_BEHAVIOR_NAME on each
 * half with the two 32-bit parameters below. The half defines that behavior
 * (node `hstatus` in config/waterfowl.keymap) and stores the values.
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
#define HALF_STATUS_OTHER_BATTERY_UNKNOWN 0xFF

// param2
#define HALF_STATUS_CONNECTED_MASK GENMASK(4, 0)   // bit n: profile n connected
#define HALF_STATUS_BONDED_MASK GENMASK(9, 5)      // bit n: profile n paired
#define HALF_STATUS_VERSION_MASK GENMASK(31, 24)   // bumped if the layout changes
#define HALF_STATUS_VERSION 1

#define HALF_STATUS_PROFILE_COUNT 5
