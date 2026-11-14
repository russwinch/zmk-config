# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**
(`config/keymap.dtsi`). Every board gets the same 34 core keys and layers;
each board's own file only adds its extras.

| Keyboard | Controller | Display / LEDs | Extras |
| --- | --- | --- | --- |
| Urchin | nice!nano v2 | nice!view gem, RGB LED widget | optional Prospector dongle |
| splitkb Aurora Corne | nice!nano v2 | nice!view gem, RGB LED widget | 2 encoders, outer thumb keys (unused outer columns); optional Prospector dongle |
| Waterfowl | nice!nano v2 | nice!view on the left half, RGB LED widget | 4 encoders, 2 extra thumb keys per side; optional Prospector dongle |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED widget | optional Prospector dongle |

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
| Config | hold Esc + Enter | computer power key, Mac/Windows switch, battery level, Bluetooth, previous/next dongle screen (U and I) (shown as "Shit" on the screens) |

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
| Prospector dongles | never (USB powered) | panel and backlight off after an hour idle (backlight PWM kept running at its shortest pulse); any key brings it back |

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
| `config/<keyboard>_layout.dtsi` | Glue shared by a keyboard's normal and dongle keymaps (Urchin, Aurora Corne, Waterfowl; the Forager dongle uses `forager.keymap` itself). |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `boards/shields/<keyboard>_dongle/` | Keyless dongle shields for the Waterfowl, Urchin and Aurora Corne (used with `prospector_adapter`); the Forager's comes from its module. |
| `src/`, `CMakeLists.txt`, `Kconfig`, `dts/` | This repo's own code (it is a Zephyr module, `zephyr/module.yml`): dims the dongle's screen when idle, the dongle's animated screens, and the left half's nice!view screen when behind the dongle. |
| `config/west.yml` | ZMK version + modules. ZMK is pinned to a `main` commit (Zephyr 4.1), modules to commits that match it. |
| `build.yaml` | Which firmware CI builds. |
| `tools/fx_preview.py` | Renders the dongle's animated screens to GIFs on a computer (see With the Prospector dongle). |
| `tools/flash.py` | Flashes a keyboard's devices in order, optionally with a settings reset (see Flashing). |
| `keymap-drawer/` | Settings and script that draw `keymap.svg` (run by `.github/workflows/draw.yml`). |

## Flashing

Every push builds all firmware (`.github/workflows/build.yml`) and publishes
it as a release named after the branch and commit (the newest 30 are kept;
builds of branches other than `main` are prereleases). Download
`firmware.zip` from the release and flash each half (double-tap reset, then
copy the `.uf2` onto the drive that appears).

[Keyboard Writer](https://github.com/russwinch/keyboard-writer) is a macOS
app that does all of this: plug the devices in, press Start, and each is
flashed in order without touching reset. It lists the releases here and
downloads the one chosen. The firmware here includes its
module (`config/west.yml`, `config/default.overlay`) so the app can identify
each device and start its bootloader over USB.

`tools/flash.py` does the copying from a terminal: it waits for each bootloader drive
and copies the right file, in order, so you only double-tap reset. It needs
Python 3.9+ and nothing else.

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

With the Prospector dongle:

| Keyboard | Dongle (`xiao_ble`) | Left half | Right half |
| --- | --- | --- | --- |
| Urchin | `urchin_dongle prospector_adapter …` | `urchin_left_peripheral_for_dongle` | `urchin_right …` |
| Aurora Corne | `splitkb_aurora_corne_dongle prospector_adapter …` | `splitkb_aurora_corne_left_peripheral_for_dongle` | `splitkb_aurora_corne_right …` |
| Waterfowl | `waterfowl_dongle prospector_adapter …` | `waterfowl_left_peripheral_for_dongle` | `waterfowl_right …` |
| Forager | `forager_dongle prospector_adapter …` | `forager_left_peripheral_for_dongle` | `forager_right …` |

If halves won't pair, flash `settings_reset` (one per controller type:
`nice_nano` or `xiao_ble`) to both halves, then the normal
firmware again.

### With the Prospector dongle

Every keyboard can run behind a Prospector dongle (`<keyboard>-dongle` in
`tools/flash.py --list`). The steps below use the Waterfowl's names; the
others follow the same pattern (table above). The dongle becomes the split central (it runs the keymap and plugs into the
computer); both halves become peripherals. To switch:

Quickest: `python3 tools/flash.py waterfowl-dongle --reset` does all of the
steps below in order. By hand:

1. Flash `settings_reset` to the dongle (`xiao_ble`) and both halves (`nice_nano`).
2. Flash `waterfowl_dongle prospector_adapter` to the dongle,
   `waterfowl_left_peripheral_for_dongle` to the left half and the normal
   `waterfowl_right` to the right half.
3. Power on the **left half first, then the right**: the Prospector orders its
   battery widgets by pairing order.

The Config layer's battery key works with the dongle too: the dongle passes it
on and each half blinks its own battery level.

Urchin and Aurora Corne halves keep nice-view-gem's peripheral screen, and
the Forager halves their RGB LED. On the Waterfowl, the left half's nice!view
(in `waterfowl_left_peripheral_for_dongle`) swaps
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
`config/<keyboard>_dongle.conf`; the animation timings are set there too.

#### Animated screens

The **Screen +** key on the Config layer (the I key) steps the dongle from
that status screen through nine full-screen animations and back to it;
**Screen -** (the U key) goes the other way. They are prototypes:

| # | Screen | Idle | Typing |
| --- | --- | --- | --- |
| 1 | Warp | drifting stars | light speed down a tunnel; each key fires comets out through its place on the screen, and the view leans towards the side being typed on |
| 2 | Fire | embers | a wall of flame; each key throws in a hot spot under its column |
| 3 | Sparks | a firework now and then | one per key, above its column |
| 4 | Terminal | a blinking cursor | what you type, in green, fading out over about 10 seconds |
| 5 | Code | a program in an alien script, written slowly | written fast: two characters a key (the same key always gives the same one), lines running off the top |
| 6 | Thermal | cold blue, two warm patches wandering | the board through a heat camera: each key leaves a hot spot at its own place that spreads and cools; a sight follows the last key |
| 7 | Tracker | a motion tracker's empty rings and pulse | contacts close in, more and nearer with speed; each key pulls one round to its column; the figure is the nearest one's range |
| 8 | Life | Conway's Game of Life, three generations a second | up to fifteen a second; each key drops a clump of cells at its own place; cells age from white through green to blue |
| 9 | Hop | a little creature strolling through a platform game, jumping what is in its way | it runs; each key plants a flower under its column and gets a hop out of it; stars it picks up are counted top right |

Terminal shows the text itself, passwords included, to anyone who can see the
dongle; it only does so while it is the screen showing. It works the
characters out as a US layout would, so on a computer set to another layout
some symbols will differ from what the computer prints. Shortcuts (anything
with Ctrl, Alt or Cmd held) aren't shown.

Warp's comets are cyan for the left hand, amber for the right and magenta for
the thumbs. The animations place a key by taking the board as 10 columns by 4
rows, which is the Waterfowl; on the other boards it is only roughly right.

Tracker and Thermal show the batteries and the output their own way, in the
animations' small pixel letters: Tracker as a gauge up each side with the
level under it and the output top right, Thermal as `85/80` bottom left and
the output bottom right. The others show them
along the bottom as the Field screen
draws them, in its fonts and colours: both levels bottom left (`85/80`, left
then right; `-` for a half that isn't connected), and bottom right the
wireless symbol with the Bluetooth profile number (counted from 0, as on the
Field screen), or a link symbol for USB. The symbol is cyan when the computer
is connected, grey when not. With another layout chosen its fonts aren't in
the firmware, and the animations fall back to small text of their own
(`85/80`, `USB` or `BT1`..; red for low, missing or not connected). The screen to start on after
power-up and the typing speed that counts as flat out are set in
`config/<keyboard>_dongle.conf`.

Only the dongle needs flashing to change these (the keymap and the screen
code both live there): pick the build in
[Keyboard Writer](https://github.com/russwinch/keyboard-writer), see
[Flashing](#flashing).

The drawing code (`src/screen_fx/fx.c`) has no firmware in it, so
`tools/fx_preview.py` can run it on a computer and save each animation as a
GIF in `fx_preview_out/` (needs gcc and Pillow): the quick way to try a change
or a new animation. Its GIFs show the small fallback status text, not the
Field one (`src/screen_fx/status_overlay.c`, which needs the module's fonts). On the dongle, `src/dongle_screen_fx.c` parks LVGL on an
empty screen and writes the frames straight to the panel, a quarter of the
pixels doubled up. The dongle has no RAM to spare for the pictures, so they
are kept in one of LVGL's drawing buffers, idle while its screen is parked.
The SPI link to the panel sets the frame rate: a whole
frame takes about 70 ms, so full-screen animations run at around 15 a second.

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
