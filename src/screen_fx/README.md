# The dongle's screens

Each build's release has a still of every screen (`screen-<n>_<name>.png`);
`tools/fx_preview.py` draws them on a computer ([`tools/README.md`](../../tools/README.md)).

## Controls (Config layer)

| Key | Does |
| --- | --- |
| Y / U | previous / next screen |
| I / O | previous / next colour theme |
| P | backlight: low, medium, high |

The screen, theme and brightness are remembered once left for 30 seconds.

## Screens

| # | Screen | What it is |
| --- | --- | --- |
| 1 | Debug | a page of readings for chasing faults: [below](#debug) |
| 2 | Status | the board's keys, batteries, signal and connections at a glance: [below](#status) |
| 3 | Typist | your typing speed, and the board as it is, its keys lighting as you type: [below](#typist) |
| 4 | Thermal | the board through a heat camera; from 50 wpm the keys catch fire |
| 5 | Tracker | a motion tracker: contacts close in with typing speed |
| 6 | Terminal | what you type, fading out (passwords included, while it is showing) |

After 15 minutes without a key, or when the lock key is let go, a
screensaver takes over: **Warp** (stars to light speed), **Code** (an alien
script written down the page) or **Life** (Conway's Game of Life). Typing
reacts in each; the first key starts the way back to your screen.

After an hour idle the panel goes off like an old television, and a key
brings it back.

The animations show the batteries and the output along the bottom. A half's
level is amber at 15% or under, red and flashing slowly at 10%, fast at 5%.
A bolt means the half is on USB power; while charging, the Typist, Status and
Debug screens show the dongle's estimate of its level with an amber `~`
(ZMK's own reads near 100 from the charger).

The typing speed is counted as a typing test does: characters less
backspaces, five to a word, over the last 10 seconds.

## Colour themes

Typist, Status, Debug, Tracker and Terminal take the theme's colours; the name
shows for a moment when it changes. Every theme keeps green, amber and red
for good, weak and lost.

Dark: ICE (the default), PHOSPHOR, AMBER, SITH, GRID, SYNTHWAVE, DRACULA,
CYBERPUNK, LCARS, MONOKAI, GRUVBOX, TOKYO NIGHT, CATPPUCCIN, NORD, MONO.
Light or coloured: GITHUB LIGHT, LATTE, GRUVBOX LIGHT, ONE LIGHT, PAPERCOLOR,
SOLARIZED, GAME BOY, NORTON, AMIGA, COBALT2.

They are one line each in `colors.h`.

## Typist

| Part | Shows |
| --- | --- |
| lights along the top | the typing speed, a light per 8 wpm: green, red, then violet from 88 |
| under them | the typing speed, big |
| the board | drawn as it is, as in the README: keys white while held and glowing after, orange holding a modifier, violet holding a layer. Encoders are knobs that turn with them, the Waterfowl's rollers rolling sideways |
| bottom left, bottom right | each half's battery, its signal's bars (green, amber, red) beside it |
| bottom middle | `BT`+profile (green when connected) or `USB` (green: listening, amber: computer asleep) |

## Status

Laid out like a racing car's steering wheel display.

| Part | Shows |
| --- | --- |
| signals along the top | each link's dBm (`--` in red when down), the computer's in the middle (over USB: `USB`, green when ready, amber when the computer sleeps) |
| connection strips | under each link, its last 40 minutes, a minute a step: green, amber or red by its weakest signal, red where it was down, a dim line where the keyboard was idle (the halves may have been asleep) |
| the keys | as they are on the board, lit as on Typist; the typing speed between the halves |
| batteries | each half's level, volts and average draw in mW (`~` while still rough), a bar, a bolt while on USB power |
| boxes along the foot | `BT`+profile or `USB`; `DR` drops since start; the dongle's supply volts and temperature; the worst trouble (`HANG`, `CRSH`, `B`+ms a bouncing switch, `CH` charging state failed, `UE` USB reports not taken) or `OK` |

## Debug

Whether each part of the keyboard works, and what has gone wrong. The typing
speed and battery levels are on Typist and Status.

| Line | Shows |
| --- | --- |
| `B149 1:12:51 ACT 40ms` | build 149 (`DEV` if not from CI); time up; `ACT`ive, `IDLE` or `SLEEP`; how long this screen takes to draw a frame |
| `RESET SOFT 02  BOUNCE 18ms` | why it last restarted (`POR`, `PIN`, `SOFT`; red `WDOG`, `LOCK`, `BRWN`; all the reasons in hex), or a red `HANG`/`CRASH` with its code and address; the quickest any key has come back down, red under 30 ms: a bouncing switch |
| `LEFT`, the keyboard's name, `RIGHT` | a column each for the halves and the dongle, red when its link is down (the dongle's is to the computer): rows below |
| profiles, `USB` | the five profiles (filled: connected, ring: paired, dot: free; the one in use underlined); USB `--`, `PWR` or `HID` |
| `MEM 62% UE3` | LVGL memory used; only if there are any, `UE` and the USB reports the computer didn't take |
| `L3 NAV ⌃⌥⇧⌘` | the top layer and its name; the modifiers, lit while held |
| last line | the last key, keycode or encoder event, with the ms since the one before |
| key map | every key, lit while held. A key that doesn't light isn't reaching the dongle |

| Row | Shows |
| --- | --- |
| `dBm` | signal: green above -75, amber to -85, red below (packets start going missing). `E` and a code when the radio wouldn't say; `USB` while typing over it |
| chart under `dBm` | the signal's last 30 minutes, a column a minute (the weakest in it): weak minutes solid amber or red, a full red column where the link was down, full over USB, nothing while idle |
| `DROPS` | times the link was lost since the dongle started, then why the last: `08` went silent, `13` the other end closed it, `16` the dongle did, `3E` never got going |
| `%` | the battery's level: amber at 15 or under, red at 10; the dongle's estimate with a `~` while charging (ZMK's reads high), a bolt while on USB power |
| `V` | the battery's volts, to the millivolt (dim while charging: the charger's); the dongle's own 3.3 V supply |
| chart under `V` | the last 30 minutes of them, a column a minute, scaled to fit (at least 60 mV top to bottom) |
| `mW` | each half's average draw (`~` while still rough) |
| `CHG` | the dongle's charging-state subscription: `OK`; amber `WT`, `Q`, `R`, `S` on the way; `NF` the half's firmware lacks it; red `E`+code or `TO`. For the dongle, its temperature |

The draw (mW) is coloured by how long a full battery would last: green four
weeks or more, plain a week or more, amber under a week, red under two days.
It is an estimate from the voltage falling, so it stays rough (`~`) for hours.
