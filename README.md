# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**
(`config/keymap.dtsi`). Every board gets the same 34 core keys and layers;
each board's own file only adds its extras.

| Keyboard | Controller | Display / LEDs | Extras |
| --- | --- | --- | --- |
| Urchin | nice!nano v2 | nice!view gem, RGB LED widget | none |
| splitkb Aurora Corne | nice!nano v2 | nice!view gem, RGB LED widget | 2 encoders, outer thumb keys (unused outer columns) |
| Waterfowl | nice!nano v2 | nice!view on the left half, RGB LED widget | 4 encoders, 2 extra thumb keys per side; optional Prospector dongle |
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
| Config | hold Esc + Enter | computer power key, Mac/Windows switch, battery level, Bluetooth (shown as "Shit" on the screens) |

The Config layer's power key sends the computer's power key; it doesn't turn
the keyboard off. macOS ignores a quick tap of it: hold it for about a second
to sleep the Mac, longer for the shut-down prompt. The battery key blinks each
half's battery level on its RGB LED (green, yellow, red). The LED also flashes
the Bluetooth connection state when it changes. Both flashes last 500ms
(set for every board in `config/default.conf`).

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

### Sleep

| Board | Deep sleep after | Display |
| --- | --- | --- |
| Urchin, Aurora Corne | 1 hour | nice!view stays on (it uses next to no power) |
| Forager | 1 hour | none |
| Waterfowl halves | 1 hour | left nice!view stays on |
| Waterfowl dongle | never (USB powered) | backlight dims very low (2%) after an hour idle; never fully off |

Deep sleep drops the Bluetooth connection: the next key press wakes the board
(that key isn't typed) and it takes a second or so to reconnect. Nothing sleeps
while it's on USB power. The sleep timeout is shared by every board in
`config/default.conf`; the dongle's exception is in `build.yaml`.

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
| `config/default.conf` | Settings shared by every build (deep sleep). ZMK applies it after the per-board files, so exceptions go in `build.yaml` as `cmake-args`. |
| `config/<shield>.conf` | Per-board Kconfig (display, encoders, LEDs, Bluetooth power). |
| `config/waterfowl_layout.dtsi` | Waterfowl glue shared by its normal and dongle keymaps. |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `boards/shields/waterfowl_dongle/` | Keyless dongle shield for the Waterfowl (used with `prospector_adapter`). |
| `src/`, `CMakeLists.txt`, `Kconfig`, `dts/` | This repo's own code (it is a Zephyr module, `zephyr/module.yml`): dims the dongle's screen when idle, and the left half's nice!view screen when behind the dongle. |
| `config/west.yml` | ZMK version + modules. ZMK is pinned to a `main` commit (Zephyr 4.1), modules to commits that match it. |
| `build.yaml` | Which firmware CI builds. |
| `tools/flasher/` | macOS app that flashes a keyboard's devices in order and shows which one to plug in next (see Flashing). |
| `tools/flash.py` | The same from the command line, on any OS. |
| `tools/targets.json` | The keyboards and their firmware files, for both of the above. |
| `keymap-drawer/` | Settings and script that draw `keymap.svg` (run by `.github/workflows/draw.yml`). |

## Flashing

Every push builds all firmware (`.github/workflows/build.yml`). Download the
zip from the latest successful run in the Actions tab and flash each half
(double-tap reset, then copy the `.uf2` onto the drive that appears).

### ZMK Flasher (macOS app)

`tools/flasher` is a small app that does the flashing for you. It draws the
keyboard and lights up the device to plug in next. When you plug that device
in, the app starts its bootloader over USB and copies the right file: no
double-tap on reset.

That works once a device runs firmware from this repo that has the USB
trigger (`src/usb_flash_trigger.c`). The first time, or if the firmware is
ever too broken to answer, double-tap reset as before and the app carries on
from there. Each device also says whether it is the left half, right half or
dongle, so the app won't flash a half with the other half's firmware.

```sh
tools/flasher/build.sh                      # needs Xcode or its command line tools
open "tools/flasher/build/ZMK Flasher.app"
```

Pick the keyboard, click a device to leave it out, and press Start. It uses
the newest `firmware*.zip` in `~/Downloads` unless you choose another.
"Reset settings first" flashes `settings_reset` before each device's
firmware, so each device is done twice (leave it plugged in). It never copies
onto a board of the wrong type.

### From the command line

`tools/flash.py` does the same in a terminal, on macOS, Linux and Windows. It
needs Python 3.9+ and nothing else.

```sh
python3 tools/flash.py waterfowl-dongle            # newest ~/Downloads/firmware*.zip
python3 tools/flash.py waterfowl-dongle --reset    # settings_reset first on each device
python3 tools/flash.py waterfowl --firmware path/to/firmware.zip
python3 tools/flash.py waterfowl-dongle --download # latest main build, needs the gh CLI
python3 tools/flash.py waterfowl-dongle --only dongle
python3 tools/flash.py --list                      # keyboards and their devices
```

With `--reset` each device is done twice (settings reset, then firmware), so
double-tap reset again when it asks. `--dry-run` shows the plan without
flashing. It stops rather than copy onto a board of the wrong type.

| Keyboard | Left half | Right half |
| --- | --- | --- |
| Urchin | `urchin_left …` | `urchin_right …` |
| Aurora Corne | `splitkb_aurora_corne_left …` | `splitkb_aurora_corne_right …` |
| Waterfowl | `waterfowl_left …` | `waterfowl_right …` |
| Forager | `forager_left …` | `forager_right …` |

If halves won't pair, flash `settings_reset` (one per controller type:
`nice_nano` or `xiao_ble`) to both halves, then the normal
firmware again.

### Waterfowl with the Prospector dongle

The dongle becomes the split central (it runs the keymap and plugs into the
computer); both halves become peripherals. To switch:

Quickest: ZMK Flasher (or `python3 tools/flash.py waterfowl-dongle --reset`)
with "Reset settings first" on does all of the steps below in order. By hand:

1. Flash `settings_reset` to the dongle (`xiao_ble`) and both halves (`nice_nano`).
2. Flash `waterfowl_dongle prospector_adapter` to the dongle,
   `waterfowl_left_peripheral_for_dongle` to the left half and the normal
   `waterfowl_right` to the right half.
3. Power on the **left half first, then the right**: the Prospector orders its
   battery widgets by pairing order.

The Config layer's battery key works with the dongle too: the dongle passes it
on and each half blinks its own battery level.

The left half's nice!view (in `waterfowl_left_peripheral_for_dongle`) swaps
ZMK's stock picture for a small status screen, read with the screen vertical:

| Part | Shows |
| --- | --- |
| `L:` / `R:` rows | each half's battery icon and % (`L:` is this half, a bolt when charging; `R:` comes from the dongle, `--` until it has heard from the right half) |
| Bottom-left icon | the dongle's output: Bluetooth or USB |
| Bottom-right icon | this half's link to the dongle (✕ when not connected) |
| Five circles | the dongle's Bluetooth profiles: filled is active, ring is connected, dashed ring is paired |

The dongle sends this when the profile, output or a battery changes, and again
every minute (`src/dongle_half_status.c`, `src/half_status_screen.c`). It uses
ZMK's existing "run a behavior on a half" message: a `hstatus` behavior in
`config/waterfowl.keymap` that no key is bound to. The `R` battery is the
dongle's last reading, so it can be out of date while the right half sleeps.

The dongle's own screen uses the Prospector module's **Field** layout (layer
name, battery levels, modifiers and a grid of lines that animate as you type).
To try Classic, Radii or Operator, change the layout line in
`config/waterfowl_dongle.conf`; the animation timings are set there too.

If one peripheral stops
sending keys after connecting to the dongle, reset that half (a known ZMK issue,
[zmk#3156](https://github.com/zmkfirmware/zmk/issues/3156)).
To go back, flash `settings_reset` again and then the normal `waterfowl_left`.

## Adding a keyboard

1. Add its modules (if any) to `config/west.yml` and its halves to `build.yaml`.
2. Create `config/<shield>.keymap` that defines `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see `splitkb_aurora_corne.keymap`
   for one with extra keys and encoders), then `#include "keymap.dtsi"`.
3. Add a row to the tables above.
