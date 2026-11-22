# Tools

## flash.py

Flashes a keyboard's devices in order (dongle, left, right): it waits for each
bootloader drive and copies the right file, so you only double-tap reset.
Python 3.9+, nothing else.

```sh
python3 tools/flash.py --list                      # keyboards and their devices
python3 tools/flash.py waterfowl-dongle            # newest ~/Downloads/firmware*.zip
python3 tools/flash.py waterfowl --firmware path/to/firmware.zip
python3 tools/flash.py waterfowl-dongle --download # latest main build (needs gh)
python3 tools/flash.py waterfowl-dongle --reset    # settings_reset first on each device
python3 tools/flash.py waterfowl-dongle --only dongle
```

With `--reset` each device is done twice, so double-tap reset again when it
asks. `--dry-run` shows the plan. It won't copy onto a board of the wrong type.

## fx_preview.py

Draws the dongle's screens on a computer (needs gcc, git and Pillow), into
`fx_preview_out/`:

```sh
tools/fx_preview.py                   # every screen as a GIF, and a contact sheet
tools/fx_preview.py --stills          # a PNG of each screen, 4x (as on each release)
tools/fx_preview.py --board waterfowl # on another board's keys (or corne)
tools/fx_preview.py --saver thermal   # each screensaver coming over Thermal
tools/fx_preview.py --power keys      # the panel going off and on
tools/fx_preview.py --themes          # every colour theme
tools/fx_preview.py --contrast        # each theme's contrast ratios, low ones flagged
```

## half_status_preview.py

Draws the left half's nice!view screen for a few made-up states, into
`half_status_preview_out/half_status.png` (needs gcc, git and Pillow).

## boards.py

Draws each keyboard to scale, from the measurements it keeps of its makers'
CAD files, into `docs/boards/` for the README (no packages needed).
`--header` also writes `src/screen_fx/boards.h`, the same shapes for the
dongle's Keys screen.
