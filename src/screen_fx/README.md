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
| 2 | Status | batteries, signal and typing speed as graphs and bars: [below](#status) |
| 3 | Keys | the three links, the keys lighting as you type, a level meter per column: [below](#keys) |
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
A bolt means the half is on USB power; while charging, the Keys, Status and
Debug screens show the dongle's estimate of its level with an amber `~`
(ZMK's own reads near 100 from the charger).

The typing speed is counted as a typing test does: characters less
backspaces, five to a word, over the last 10 seconds.

## Colour themes

Keys, Status, Debug, Tracker and Terminal take the theme's colours; the name
shows for a moment when it changes. Every theme keeps green, amber and red
for good, weak and lost.

Dark: ICE (the default), PHOSPHOR, AMBER, SITH, GRID, SYNTHWAVE, DRACULA,
CYBERPUNK, LCARS, MONOKAI, GRUVBOX, TOKYO NIGHT, CATPPUCCIN, NORD, MONO.
Light or coloured: GITHUB LIGHT, LATTE, GRUVBOX LIGHT, ONE LIGHT, PAPERCOLOR,
SOLARIZED, GAME BOY, NORTON, AMIGA, COBALT2.

They are one line each in `colors.h`.

## Keys

| Part | Shows |
| --- | --- |
| top left, top right | each half: battery, signal in dBm (green, amber, red; `--` when not connected), and under it the signal over the last 40 seconds, weak seconds as solid amber or red bars |
| top middle | on Bluetooth: the five profiles (filled: connected, ring: paired, dot: free; the selected one underlined) and the computer's signal. Over USB: `USB` (green: listening, amber: computer asleep), the typing speed and a graph of it |
| the keys | white while held and glowing after; orange holding a modifier, violet holding a layer. Encoders are knobs that turn with them |
| under the keys | a level meter per column, as on a hi-fi |

## Status

Laid out like a racing car's steering wheel display.

| Part | Shows |
| --- | --- |
| lights along the top | the typing speed, a light per 8 wpm: green, red, then violet from 88. A white light marks the fastest lately |
| left and right | each half's keys, as on Keys; the speed big between them |
| signals | each link as on Keys, the computer's in the middle (over USB: `USB` and a graph of the typing speed) |
| connection strips | under each link, its last 80 minutes, 2 minutes a step: green, amber or red by its weakest signal, red where it was down, a dim line where the keyboard was idle (the halves may have been asleep) |
| batteries | each half's level, volts and average draw in mW (`~` while still rough), a bar, a bolt while on USB power |
| boxes along the foot | `BT`+profile or `USB`; `DR` drops since start; the dongle's supply volts and temperature; the worst trouble (`HANG`, `CRSH`, `B`+ms a bouncing switch, `CH` charging state failed, `UE` USB reports not taken) or `OK` |

## Debug

| Line | Shows |
| --- | --- |
| `B149 1:12:46 ACT 10F 38MS` | build 149 (`DEV` if not from CI); uptime; `ACT`ive, `IDL`e or `SLP`; frames a second and ms per frame |
| `RST SOFT 02  MEM 62% ^71` | why it last restarted (`POR`, `PIN`, `SOFT`; red `WDOG`, `LOCK`, `BRWN`, `HANG`, `CRSH` with a code and address); LVGL memory used and peak |
| `L`, `R`, `H` rows | the links to the left half, right half and computer (letter red when down): columns below |
| profiles, `USB`, `RPT`, `E` | the five profiles; USB `--`/`PWR`/`HID`; reports sent over USB and how many failed |
| `L3 NAV 0009 ⌃⌥⇧⌘ 51WPM` | top layer and its name, active layers (hex), held modifiers, typing speed |
| `DRAW L1.42 R~0.9mW 31C` | each half's average draw; the dongle's temperature. A missing reading says why in amber: `WT` not yet, `TO` timed out, `NB` not built, `E1`–`E4` failed |
| three event lines | the last key, keycode and encoder events, with ms between |
| key map | every key, lit while held. A key that doesn't light isn't reaching the dongle |
| `BNC 18MS P23` | the shortest up-and-down of any key; red under 30 ms: a bouncing switch |

| Column | Shows |
| --- | --- |
| `DBM` | signal: green above -75, amber to -85, red below (packets start going missing). `E` and a code when the radio wouldn't say |
| `%` | battery (the estimate with `~` while charging), with a bolt on USB power. On `H`, the output |
| `V` | battery to the millivolt (dim while charging); on `H` the dongle's 3.3 V supply |
| `DR` | drops since the dongle started |
| `RS` | why the last drop: `08` went silent, `13` the other end closed it, `16` the dongle did, `3E` never got going |
| `CH` | the dongle's charging-state subscription: `OK`; amber `WT`, `Q1`, `R1`, `S1` on the way; `NF` the half's firmware lacks it; red `E`+code or `TO` |
| `KEY` | key presses since start |

The draw (mW) is coloured by how long a full battery would last: green four
weeks or more, plain a week or more, amber under a week, red under two days.
It is an estimate from the voltage falling, so it stays rough (`~`) for hours.
