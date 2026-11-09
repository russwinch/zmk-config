# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**
(`config/keymap.dtsi`). Every board gets the same 34 core keys and layers;
each board's own file only adds its extras. All four keyboards broadcast
their status for a [Prospector](https://github.com/carrefinho/prospector)
scanner (see below).

| Keyboard | Controller | Display / LEDs | Extras |
| --- | --- | --- | --- |
| Urchin | nice!nano v2 | nice!view gem, RGB LED widget | none |
| splitkb Aurora Corne | nice!nano v2 | nice!view gem, RGB LED widget | 2 encoders, outer thumb keys (unused outer columns) |
| Waterfowl | nice!nano v2 | nice!view on the left half, RGB LED widget | 4 encoders, 2 extra thumb keys per side |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED widget | none |

## Keymap

![All layers](keymap-drawer/keymap.svg)

The picture is generated: the "Draw keymap" workflow redraws it and commits it
whenever the keymap changes. To preview locally, run `keymap-drawer/draw.sh`
(needs `pip install keymap-drawer`). Modifiers are drawn with Mac symbols; on
the Windows layer read ⌘ as Win.

| Layer | How to get there | What's on it |
| --- | --- | --- |
| Mac | default | QWERTY, home-row mods Ctrl Alt Cmd Shift / Shift Cmd Alt Ctrl |
| Windows | Config layer → Windows | Mac layer with Ctrl and Win swapped |
| Lower | hold Esc | media, browser tabs, desktops, Mission Control, arrows, symbols |
| Raise | hold Enter | numbers, shifted numbers, brackets |
| Config | hold Esc + Enter | power, Mac/Windows switch, battery level, Bluetooth (shown as "Shit" on the screens) |

### Timing

- **Home-row mods** use urob's "timeless" setup: a mod only fires when the next
  key is on the other hand or a thumb. `require-prior-idle-ms` is 100 (urob
  uses 150), so shortcuts typed right after a word still work.
- **Space/Shift thumb** (`&ts`) is balanced: Shift as soon as a key is pressed
  and released while Space is held, so fast capitals work.
- **Backspace/Cmd thumb** is tap-preferred with a 250ms term, so rolling
  Backspace into a letter never sends Cmd+letter.
- **Esc/Lower and Enter/Raise** are hold-preferred: any key pressed while the
  thumb is down uses the layer.

### Board extras

| Board | Encoders (turn) | Extra keys |
| --- | --- | --- |
| Urchin, Forager | none | none |
| Aurora Corne | left: volume; right: up/down (page up/down on Lower) | outer thumbs: mute (left), play/pause (right) |
| Waterfowl | left roller (inner): page up/down; left dial (outer): volume; right dial (outer): up/down (page up/down on Lower); right roller (inner): left/right | pushes: left dial mute, left roller mute, right roller nothing, right dial play/pause; the outer thumb keys repeat the thumb next to them |

## Layout of this repo

| File | Purpose |
| --- | --- |
| `config/keymap.dtsi` | **The keymap.** All layers and behaviors, written once as 34 core keys. |
| `config/<shield>.keymap` | Per-board glue: maps the 34 core keys onto that board (extra keys, encoders), then includes `keymap.dtsi`. |
| `config/<shield>.conf` | Per-board Kconfig (display, encoders, sleep, Bluetooth power). |
| `config/<shield>_left.conf` | Left (central) half only: Prospector scanner status advertising and name. |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `config/west.yml` | Keyboards: ZMK version + modules, all pinned to match ZMK v0.3. |
| `build.yaml` | Which keyboard firmware CI builds (`.github/workflows/build.yml`). |
| `keymap-drawer/` | Settings and script that draw `keymap.svg` (run by `.github/workflows/draw.yml`). |

## Flashing

Every push builds all firmware (`.github/workflows/build.yml`). Download the
zip from the latest successful run in the Actions tab and flash each half
(double-tap reset, then copy the `.uf2` onto the drive that appears).

| Keyboard | Left half | Right half |
| --- | --- | --- |
| Urchin | `urchin_left …` | `urchin_right …` |
| Aurora Corne | `splitkb_aurora_corne_left …` | `splitkb_aurora_corne_right …` |
| Waterfowl | `waterfowl_left …` | `waterfowl_right …` |
| Forager | `forager_left …` | `forager_right …` |

If halves won't pair, flash `settings_reset` (one per controller type:
`nice_nano_v2` or `seeeduino_xiao_ble`) to both halves, then the normal
firmware again.

## Prospector scanner

The Prospector runs [t-ogura's scanner firmware](https://github.com/t-ogura/zmk-config-prospector):
it never pairs with anything, it just listens. Each keyboard keeps connecting
to the computer as normal, and its central (left) half broadcasts layer,
battery, modifiers and connection status for the scanner to show.

- **Keyboards:** nothing to do beyond flashing the normal firmware above. The
  `config/<shield>_left.conf` files (central half only) turn status advertising on and set the name the scanner shows
  (Urchin, Corne, Waterfwl, Forager: max 8 characters). That name also
  becomes the keyboard's Bluetooth name.
- **Scanner:** built in its own repo,
  [russwinch/zmk-prospector-scanner](https://github.com/russwinch/zmk-prospector-scanner)
  (fork of t-ogura's). Download its latest `firmware` zip and flash
  `prospector_scanner.uf2` to the Prospector, or
  `prospector_scanner_touch.uf2` if yours has the touch display. If it was
  running other firmware before (e.g. a dongle build), flash its
  `settings_reset` first.

The module comes from the `scanner` branch of
[russwinch/prospector-zmk-module](https://github.com/russwinch/prospector-zmk-module)
(t-ogura v2.2.3), pinned by the same commit here and in the scanner repo.
The scanner needs ZMK main (Zephyr 4), which is why it lives in its own repo;
the keyboards stay on ZMK v0.3. If the Prospector doesn't see a
keyboard, power-cycle the keyboard: it sends a burst of updates at startup.

## Adding a keyboard

1. Add its modules (if any) to `config/west.yml` and its halves to `build.yaml`.
2. Create `config/<shield>.keymap` that defines `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see `splitkb_aurora_corne.keymap`
   for one with extra keys and encoders), then `#include "keymap.dtsi"`.
3. Add a row to the tables above.
