/* What the dongle hears from each half (0 left, 1 right) beyond ZMK's own:
 * whether it is on USB power, and its battery's voltage. src/dongle_half_info.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// On USB power, as last heard. False when not known.
bool dongle_half_charging(int half);

// The battery's millivolts, as last heard (the charger's while on USB
// power). 0 when not known: not connected, or the half's firmware is from
// before it sent them.
uint16_t dongle_half_millivolts(int half);

// The half's average draw in hundredths of a milliwatt, from its voltage since it
// was last unplugged (or the dongle started), or -1 until there is enough
// of it; `rough` while it may still be well out (or settling after unplugging).
// src/dongle_half_drain.c.
int dongle_half_drain_hundredths_mw(int half, bool *rough);

// The half's battery millivolts every `every_min` minutes, the newest last
// in `mv[0..n-1]`, 0 where not known (not connected, or not that long
// since the dongle started). src/dongle_half_drain.c.
void dongle_half_trend(int half, uint16_t *mv, int n, int every_min);

// Where the dongle has got to with the half's charging state, for the Debug
// screen: up to two letters in `code` ("" when the half isn't connected):
//   WT  waiting for ZMK to finish setting the half's link up
//   Q1  queued (behind the other half, or waiting to retry), and the try
//   R1  the read is out; S1 the subscription is out
//   OK  subscribed: the half says when it changes
//   NF  the half has no such characteristic (firmware before it)
//   E   gave up after a few tries: `err` says why
//   TO  a request had no answer in 10 s: left alone until it reconnects
// and in `err` the last error: an ATT error code, or 0x80 | a Zephyr errno;
// 0 for none.
void dongle_half_info_progress(int half, char code[3], uint8_t *err);
