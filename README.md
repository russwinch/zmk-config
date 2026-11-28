# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**
(`config/keymap.dtsi`). Every board gets the same 34 core keys and layers;
each board's own file only adds its extras.

| Keyboard | Controller | Display / LEDs | Extras |
| --- | --- | --- | --- |
| Urchin | nice!nano v2 | RGB LED widget (no screens) | optional Prospector dongle |
| splitkb Aurora Corne | nice!nano v2 | nice!view on the left half, RGB LED widget | 2 encoders, outer thumb keys, an outer column per side; optional Prospector dongle |
| Waterfowl | nice!nano v2 | nice!view on the left half, RGB LED widget | 4 encoders, an extra thumb key per side; optional Prospector dongle |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED widget | optional Prospector dongle |

More lives beside what it describes:

- [`src/screen_fx/README.md`](src/screen_fx/README.md): the dongle's screens.
- [`tools/README.md`](tools/README.md): flashing from a terminal, previewing
  the screens on a computer.

## Keymap

![All layers](keymap-drawer/keymap.svg)

Redrawn by the "Draw keymap" workflow whenever the keymap changes. Modifiers
are drawn with Mac symbols; on the Windows layer read ⌘ as Win.

Every board has those 34 keys; what each adds is in amber:

| [![Urchin](docs/boards/urchin.svg)](docs/boards/urchin.svg) | [![Forager](docs/boards/forager.svg)](docs/boards/forager.svg) | [![Aurora Corne](docs/boards/corne.svg)](docs/boards/corne.svg) | [![Waterfowl](docs/boards/waterfowl.svg)](docs/boards/waterfowl.svg) |
| :---: | :---: | :---: | :---: |
| Urchin | Forager | Aurora Corne | Waterfowl |

Drawn to scale from each maker's CAD files by `tools/boards.py`.

| Board | Encoders (turn) | Extra keys |
| --- | --- | --- |
| Aurora Corne | left: volume; right: up/down (page up/down on Lower) | outer thumbs: mute (left), play/pause (right); outer columns, top to bottom: Tab, Esc (hold: Ctrl), Shift on the left, Del, ', Enter (hold: Shift) on the right |
| Waterfowl | left roller: scroll up/down; left dial: volume; right dial: up/down (page up/down on Lower); right roller: scroll left/right | pushes: left roller Home, right roller End, left dial mute, right dial play/pause; the outer thumb keys repeat the thumb next to them. Over Bluetooth the computer has to forget and re-pair the keyboard before it scrolls |

| Layer | How to get there | What's on it |
| --- | --- | --- |
| Mac | default | QWERTY, home-row mods Ctrl Alt Cmd Shift / Shift Cmd Alt Ctrl |
| Windows | Config layer → Windows | Mac layer with Ctrl and Win swapped |
| Lower | hold Esc | lock screen (Q), media, browser tabs, desktops, app windows (E), screenshots (X: an area to the clipboard; Z: the whole screen to a file), Mission Control (C), the app switcher (B: Cmd+Tab, V: Cmd+Shift+Tab; Cmd stays held until Lower is let go), £ (comma), arrows, symbols |
| Raise | hold Enter | numbers, shifted numbers, brackets |
| Config | hold Esc + Enter | Mac/Windows switch, battery level, Bluetooth, dongle screen prev/next (Y, U), colour theme prev/next (I, O), brightness (P) |

The battery key blinks each half's level on its RGB LED (green, yellow, red).
On a dongle the lock key also starts the screensaver.

### Timing

- **Home-row mods** use urob's "timeless" setup: a mod only fires when the next
  key is on the other hand or a thumb. `require-prior-idle-ms` is 100 (urob
  uses 150), so shortcuts typed right after a word still work.
- **Space/Shift thumb** is balanced: Shift as soon as a key is pressed and
  released while Space is held, so fast capitals work.
- **Backspace/Cmd thumb** is tap-preferred with a 250ms term, so rolling
  Backspace into a letter never sends Cmd+letter.
- **Esc/Lower and Enter/Raise** are hold-preferred: any key pressed while the
  thumb is down uses the layer.

### Sleep

Every board deep-sleeps after an hour without a key (never on USB power); the
next key wakes it (that key isn't typed) and it reconnects in a second or so.
A nice!view stays on. The dongle never sleeps; its panel goes off after an
hour idle.

## Flashing

Every push builds all firmware and publishes a release, `build-<run number>`
(branches other than `main` are prereleases). Each has `firmware.zip`, each
`.uf2` on its own, and a still of each dongle screen (`screen-<n>_<name>.png`, and
`screen-<n>_<name>_<board>.png` for those that draw the board).

**[Keyboard Writer](https://github.com/russwinch/keyboard-writer)** (macOS) is
the easy way: pick a release, plug the devices in, press Start.

By hand: double-tap reset and copy the `.uf2` onto the drive that appears. On
an iPad, download it in Safari and Share > Save to Files onto the drive (copy
and paste in Files fails with error 100093). From a terminal:
[`tools/flash.py`](tools/README.md).

| Keyboard | Left half | Right half | Dongle (`xiao_ble`) | Left half behind the dongle |
| --- | --- | --- | --- | --- |
| Urchin | `urchin_left` | `urchin_right` | `urchin_dongle prospector_adapter` | `urchin_left_peripheral_for_dongle` |
| Aurora Corne | `splitkb_aurora_corne_left` | `splitkb_aurora_corne_right` | `splitkb_aurora_corne_dongle prospector_adapter` | `splitkb_aurora_corne_left_peripheral_for_dongle` |
| Waterfowl | `waterfowl_left` | `waterfowl_right` | `waterfowl_dongle prospector_adapter` | `waterfowl_left_peripheral_for_dongle` |
| Forager | `forager_left` | `forager_right` | `forager_dongle prospector_adapter` | `forager_left_peripheral_for_dongle` |

### Settings reset

Flash `settings_reset` (`nice_nano` or `xiao_ble`) before the firmware when
moving to or from the dongle, when the Bluetooth settings change, or when the
halves won't pair. It clears every pairing, the computer's too. Commits that
need one are tagged `settings-reset/<why>` (a plain tag, not `git tag -a`),
and Keyboard Writer then turns its reset on by itself.

## The Prospector dongle

The dongle becomes the central (runs the keymap, plugs into the computer) and
both halves become peripherals. To switch: settings reset everything, flash
the dongle and both halves (the table above; the right half's file doesn't
change), then power on the **left half first**, so the halves are in order.
To go back, settings reset and flash the normal left half.

A keymap or screen change only needs the dongle flashed. If a half stops
sending keys after connecting, reset it
([zmk#3156](https://github.com/zmkfirmware/zmk/issues/3156)).

The dongle's screens, as this commit draws them (redrawn by the "Draw screens"
workflow); what each shows: [`src/screen_fx/README.md`](src/screen_fx/README.md).

| <img src="docs/screens/4_terminal.png" width="195" alt="Terminal"> | <img src="docs/screens/6_warp.png" width="195" alt="Warp"> | <img src="docs/screens/7_code.png" width="195" alt="Code"> | <img src="docs/screens/8_life.png" width="195" alt="Life"> |
| :---: | :---: | :---: | :---: |
| Terminal | Warp (screensaver) | Code (screensaver) | Life (screensaver) |

The screens that draw the board, on each board's dongle:

| | Debug | Status | Typist | Clash | Showroom (screensaver) |
| --- | :---: | :---: | :---: | :---: | :---: |
| Urchin | <img src="docs/screens/1_debug_urchin.png" width="150" alt="Debug, Urchin"> | <img src="docs/screens/2_status_urchin.png" width="150" alt="Status, Urchin"> | <img src="docs/screens/3_typist_urchin.png" width="150" alt="Typist, Urchin"> | <img src="docs/screens/5_clash_urchin.png" width="150" alt="Clash, Urchin"> | <img src="docs/screens/9_showroom_urchin.png" width="150" alt="Showroom, Urchin"> |
| Forager | <img src="docs/screens/1_debug_forager.png" width="150" alt="Debug, Forager"> | <img src="docs/screens/2_status_forager.png" width="150" alt="Status, Forager"> | <img src="docs/screens/3_typist_forager.png" width="150" alt="Typist, Forager"> | <img src="docs/screens/5_clash_forager.png" width="150" alt="Clash, Forager"> | <img src="docs/screens/9_showroom_forager.png" width="150" alt="Showroom, Forager"> |
| Aurora Corne | <img src="docs/screens/1_debug_corne.png" width="150" alt="Debug, Aurora Corne"> | <img src="docs/screens/2_status_corne.png" width="150" alt="Status, Aurora Corne"> | <img src="docs/screens/3_typist_corne.png" width="150" alt="Typist, Aurora Corne"> | <img src="docs/screens/5_clash_corne.png" width="150" alt="Clash, Aurora Corne"> | <img src="docs/screens/9_showroom_corne.png" width="150" alt="Showroom, Aurora Corne"> |
| Waterfowl | <img src="docs/screens/1_debug_waterfowl.png" width="150" alt="Debug, Waterfowl"> | <img src="docs/screens/2_status_waterfowl.png" width="150" alt="Status, Waterfowl"> | <img src="docs/screens/3_typist_waterfowl.png" width="150" alt="Typist, Waterfowl"> | <img src="docs/screens/5_clash_waterfowl.png" width="150" alt="Clash, Waterfowl"> | <img src="docs/screens/9_showroom_waterfowl.png" width="150" alt="Showroom, Waterfowl"> |

### The left half's nice!view

| Part | Shows |
| --- | --- |
| Top: `L` | this half's battery. While charging, a bolt: behind the dongle with the dongle's estimate of the level, on its own alone (the reading is the charger's) |
| Middle: `R` | the right half's battery (`--` until heard from), or a bolt while it charges |
| Bottom | the output: USB, or Bluetooth and the profile (filled: connected; dashed: paired; plain: free). ↻ waiting for the dongle, ✕ not connected to it |

## Adding a keyboard

1. Add its modules to `config/west.yml`, its builds to `build.yaml` and to
   `TARGETS` in `tools/flash.py`.
2. Create `config/<shield>.keymap` defining `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see
   `splitkb_aurora_corne.keymap` for extra keys and encoders), then
   `#include "keymap.dtsi"`.
3. Add it to the tables above.
