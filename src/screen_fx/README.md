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
script written down the page), **Life** (Conway's Game of Life) or
**Showroom** (the keyboard in 3D, cel-shaded and filmed like a car advert:
the camera pulls back from the dongle's own screen, then cuts between shots
of the board, its name and parts captioned in the letterbox). Typing reacts
in each (in Showroom the keys go down and glow); the first key starts the
way back to your screen.

After an hour idle the panel goes off like an old television, and a key
brings it back.

The animations show the batteries and the output along the bottom. A half's
level is amber at 15% or under, red and flashing slowly at 10%, fast at 5%.
A bolt means the half is on USB power; while charging, the Typist, Status and
Debug screens show the dongle's estimate of its level (ZMK's own reads near
100 from the charger).

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
| the board | drawn as it is, as in the README: keys white while held and glowing after, orange holding a modifier, violet holding a layer; all of a half red while it isn't connected. Encoders are knobs that turn with them, the Waterfowl's rollers rolling sideways |
| bottom left, bottom right | each half's battery, its signal's bars (green, amber, red) beside it |
| bottom middle | the waves and the profile number (green when connected, white when paired, dim when free) or `USB` (green: listening, amber: computer asleep); after you switch profile, the new one flashes here for 3 s, even over USB |

## Status

Laid out like a racing car's steering wheel display.

| Part | Shows |
| --- | --- |
| signals along the top | each link's dBm (`--` in red when down); in the middle the waves and the profile number (coloured as on Typist) and the computer's dBm, or over USB `USB` (green when ready, amber when the computer sleeps); after you switch profile, the new one flashes there for 3 s |
| connection strips | under each Bluetooth link (none for the computer over USB), its last 40 minutes, a minute a step: a dim line while all was well, and only trouble in colour, taller the worse: a short amber line for a weak signal, a taller red one for a red signal, a full-height red one where the link was down; a gap where the keyboard was idle (the halves may have been asleep) |
| the keys | as they are on the board, lit as on Typist |
| between the halves | only if something has gone wrong, one a line, worst at the top: `HANG` or `CRSH` (the dongle restarted), `CH` charging state failed, `UE` USB reports not taken, `DR` the computer's link dropped, the dongle's temperature from 60 °C |
| over each battery | `DR` and how often that half's link has dropped, if it has |
| batteries | each half's level, volts and average draw in mW (`~` while still rough), and a bar, which fills step by step while that half charges; between the bars a bolt while either half is on USB power |

## Debug

Whether each part of the keyboard works, and what has gone wrong. The typing
speed is on Typist, the battery levels on Typist and Status.

| Line | Shows |
| --- | --- |
| `B149 1:12:51 ACT 40ms` | build 149 (`DEV` if not from CI); time up; `ACT`ive, `IDLE` or `SLEEP`; how long this screen takes to draw a frame |
| `RESET SOFT 02  BOUNCE 18ms` | why it last restarted (`POR`, `PIN`, `SOFT`; red `WDOG`, `LOCK`, `BRWN`; all the reasons in hex), or a red `HANG`/`CRASH` with its code and address; the quickest any key has come back down, amber under 30 ms: maybe a bouncing switch |
| `LEFT`, the keyboard's name, `RIGHT` | a column each for the halves and the dongle, red when its link is down (the dongle's is to the computer): rows below |
| waves, `USB` | the Bluetooth profile in use, as over the animations (green: connected, white: paired, dim: free; flashing for 3 s after you switch); USB `--`, `PWR` or `HID` |
| `MEM 62% UE3` | LVGL memory used; only if there are any, `UE` and the USB reports the computer didn't take |
| `L3 NAV ⌃⌥⇧⌘` | the top layer and its name; the modifiers, lit while held |
| last line | the last key, keycode or encoder event, with the ms since the one before |
| key map | every key, lit while held; all of a half red while it isn't connected. A key that doesn't light isn't reaching the dongle |

| Row | Shows |
| --- | --- |
| `dBm` | signal: green above -85 (all is well), amber to -90 (packets start going missing: move the dongle closer), red below (the link is about to drop). `E` and a code when the radio wouldn't say; `USB` while typing over it |
| chart under `dBm` | the signal's last 30 minutes, a column a minute (the weakest in it): weak minutes solid amber or red, a full red column where the link was down, full over USB, nothing while idle |
| `DROPS` | times the link was lost since the dongle started, then why the last: `08` went silent, `13` the other end closed it, `16` the dongle did, `3E` never got going |
| `%` | the battery's level: amber at 15 or under, red at 10; the dongle's estimate while charging (ZMK's reads high), a bolt while on USB power |
| `V` | the battery's volts, to the millivolt (dim while charging: the charger's); the dongle's own 3.3 V supply |
| chart under `V` | the last 30 minutes of them, a column a minute, scaled to fit (at least 60 mV top to bottom) |
| `mW` | each half's average draw (`~` while still rough) |
| `CHG` | the dongle's charging-state subscription: `OK`; amber `WT`, `Q`, `R`, `S` on the way; `NF` the half's firmware lacks it; red `E`+code or `TO`. For the dongle, its temperature |

The draw (mW) is coloured by how long a full battery would last: green four
weeks or more, plain a week or more, amber under a week, red under two days.
It is an estimate from the voltage falling, so it stays rough (`~`) for hours.
