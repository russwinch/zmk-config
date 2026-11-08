# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**.

| Keyboard | Controller | Extras |
| --- | --- | --- |
| Urchin | nice!nano v2 | nice!view (gem) |
| splitkb Aurora Corne | nice!nano v2 | nice!view (gem), 2 encoders, outer thumb keys |
| Waterfowl | nice!nano v2 | nice!view, 4 encoders, optional Prospector dongle |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED |

## Keymap

![All layers](keymap-drawer/keymap.svg)

Home-row mods use urob's "timeless" settings (see the comment in
`config/keymap.dtsi`). The picture is generated, not hand-drawn: the
"Draw keymap" workflow redraws it and commits it whenever the keymap changes.
To preview locally, run `keymap-drawer/draw.sh` (needs `pip install keymap-drawer`).

## Layout of this repo

| File | Purpose |
| --- | --- |
| `config/keymap.dtsi` | **The keymap.** All layers and behaviors, written once as 34 core keys. |
| `config/<shield>.keymap` | Per-board glue: maps the 34 core keys onto that board (extra keys, encoders, hardware tweaks), then includes `keymap.dtsi`. |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `config/waterfowl_layout.dtsi` | Waterfowl glue shared by its normal and dongle keymaps. |
| `boards/shields/waterfowl_dongle/` | Keyless dongle shield for the Waterfowl (used with `prospector_adapter`). |
| `config/<shield>.conf` | Per-board Kconfig (display, encoders, sleep). |
| `config/west.yml` | ZMK version + modules. All pinned to ZMK v0.3. |
| `build.yaml` | Which firmware CI builds. |
| `keymap-drawer/` | Settings and script that draw `keymap.svg` from the keymap (run by `.github/workflows/draw.yml`). |

### Adding a keyboard

1. Add its modules (if any) to `config/west.yml` and its halves to `build.yaml`.
2. Create `config/<shield>.keymap` that defines `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see `splitkb_aurora_corne.keymap`
   for one with extra keys and encoders), then `#include "keymap.dtsi"`.

## Waterfowl with the Prospector dongle

The dongle becomes the split central (it runs the keymap and plugs into the
computer); both halves become peripherals. To switch:

1. Flash `settings_reset` to the dongle (`seeeduino_xiao_ble`) and both halves (`nice_nano_v2`).
2. Flash `waterfowl_dongle prospector_adapter` to the dongle,
   `waterfowl_left_peripheral_for_dongle` to the left half and the normal
   `waterfowl_right` to the right half.
3. Power on the **left half first, then the right**: the Prospector orders its
   battery widgets by pairing order.

To go back, flash `settings_reset` again and then the normal `waterfowl_left`.

## Firmware

Download the zip from the latest successful run in the Actions tab and flash
each half. Flash `settings_reset` first if halves won't pair.
