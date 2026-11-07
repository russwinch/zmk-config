# zmk-config

ZMK firmware for all my keyboards, built from **one shared keymap**.

| Keyboard | Controller | Extras |
| --- | --- | --- |
| Urchin | nice!nano v2 | nice!view (gem) |
| splitkb Aurora Corne | nice!nano v2 | nice!view (gem), 2 encoders, outer thumb keys |
| Waterfowl | nice!nano v2 | nice!view, 4 encoders |
| Forager | Seeed XIAO nRF52840 | onboard RGB LED |

## Layout of this repo

| File | Purpose |
| --- | --- |
| `config/keymap.dtsi` | **The keymap.** All layers and behaviors, written once as 34 core keys. |
| `config/<shield>.keymap` | Per-board glue: maps the 34 core keys onto that board (extra keys, encoders, hardware tweaks), then includes `keymap.dtsi`. |
| `config/leds_nice_nano_rgb.dtsi` | RGB LED wiring shared by the nice!nano boards (for zmk-rgbled-widget). |
| `config/<shield>.conf` | Per-board Kconfig (display, encoders, sleep). |
| `config/west.yml` | ZMK version + modules. All pinned to ZMK v0.3. |
| `build.yaml` | Which firmware CI builds. |

### Adding a keyboard

1. Add its modules (if any) to `config/west.yml` and its halves to `build.yaml`.
2. Create `config/<shield>.keymap` that defines `KEYS_L`, `KEYS_R`, `THUMBS`,
   `KB_LAYOUT(...)` and `KB_LAYOUT_BASE(...)` (see `splitkb_aurora_corne.keymap`
   for one with extra keys and encoders), then `#include "keymap.dtsi"`.

## Firmware

Download the zip from the latest successful run in the Actions tab and flash
each half. Flash `settings_reset` first if halves won't pair.
