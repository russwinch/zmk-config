/*
 * Drawing for the left half's nice!view behind the dongle
 * (src/half_status_screen.c). Plain LVGL, nothing from ZMK, so
 * tools/half_status_preview.py can render it on a computer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

// The screen as it is read: upright, 68 wide and 160 tall (the panel itself
// is 160x68; the screen code rotates the picture onto it).
#define HALF_STATUS_DRAW_W 68
#define HALF_STATUS_DRAW_H 160

#define HALF_STATUS_DRAW_UNKNOWN 0xFF // a battery level not known (yet)

// Everything the screen shows. Compared byte for byte to skip redraws that
// would change nothing, so zero it (memset) before filling it in.
struct half_status_view {
    uint8_t own_battery;   // this (left) half, 0-100 or HALF_STATUS_DRAW_UNKNOWN
    uint8_t other_battery; // right half, from the dongle
    bool charging;         // this half is on USB power
    bool other_charging;   // the right half is
    bool estimate;         // own_battery is the dongle's estimate (ZMK's reads high under the
                           // charger): shown, with a flashing bolt while charging, not just a bolt
    bool other_estimate;   // and other_battery
    bool bolt_off;         // those bolts are in the off half of their flash (under 100)
    bool linked;           // this half is connected to the dongle
    bool valid;            // the dongle has sent its status since then
    bool usb;              // the dongle is typing over USB
    uint8_t profile;       // dongle's active Bluetooth profile (0-based)
    bool profile_connected;
    bool profile_bonded;
};

// Draws the whole screen onto an upright HALF_STATUS_DRAW_W x
// HALF_STATUS_DRAW_H canvas.
void half_status_draw(lv_obj_t *canvas, const struct half_status_view *view, lv_color_t fg,
                      lv_color_t bg);
