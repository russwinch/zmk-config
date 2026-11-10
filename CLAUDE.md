# CLAUDE.md

## Git workflow: trunk-based

- Merging straight to `main` is allowed and preferred; no pull request is
  needed unless asked for.
- Before updating `main`, wait for the firmware build (`.github/workflows/build.yml`)
  to pass on the branch, then fast-forward `main` to it
  (`git push origin <branch>:main`). Don't force-push `main`.
- If `main` has moved on, rebase the branch onto it first and wait for the
  build again.

## Repo notes

- The keymap lives in `config/keymap.dtsi`; each `config/<shield>.keymap` only
  maps the 34 core keys onto that board. See README.md.
- The firmware only really gets checked by CI, so check the build run after
  pushing keymap changes.
- `keymap-drawer/keymap.svg` is redrawn and committed by the "Draw keymap"
  workflow; pull before pushing after a keymap change.
- Keep the README's tables (boards, layers, timings, encoders) in step with
  the keymap.
- `tools/targets.json` mirrors `build.yaml` (it feeds `tools/flash.py` and the
  macOS app in `tools/flasher`): when a build is added, renamed or gets an
  `artifact-name`, update both (and `--dry-run` flash.py).
- Every build has a USB serial port (`config/default.overlay`,
  `src/usb_flash_trigger.c`) that the flasher app uses to identify the device
  and start its bootloader. ZMK reads `default.overlay` only when `config/`
  has no shield- or board-named overlay, so a new one must `#include` it.
- `tools/flasher` is SwiftUI on a plain-Foundation `FlasherCore`. It only
  builds on macOS; the "Flasher app" workflow tests it, runs the built app
  against pretend drives (`smoke.sh`) and uploads screenshots, so check that
  run after changing it.

## Flashing for the user (`tools/flash.py`)

The user normally flashes with the app (`tools/flasher`); this is for when
they ask Claude to drive it.

Only possible when Claude runs on the user's own computer: a cloud session
can't see USB drives, so there just tell them the command to run.

1. Agree the keyboard (`--list`) and whether to `--reset`. A reset clears all
   Bluetooth pairings, including the computer's, so only add it when asked or
   when pairing is broken (halves not connecting, wrong order on the dongle).
2. Make sure the firmware is current: `--download` takes the latest successful
   `main` build (needs `gh`); after pushing, wait for that build first. Or use
   `--firmware <zip or folder>`.
3. Run with `--dry-run` and check the plan.
4. Run it for real in the background. It never reads input: it prints a
   `>>>` line naming the next device, then waits up to `--timeout` seconds
   for its bootloader drive. Relay each `>>>` instruction to the user
   ("double-tap reset on the LEFT half") and report errors as printed.
5. Never pass `--force` (copies onto a board of the wrong type) unless the
   user has confirmed which device is plugged in.
6. After a `--reset` on `waterfowl-dongle`, remind them: dongle stays powered,
   and re-pair the computer (forget the keyboard there first).
