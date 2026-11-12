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
- `tools/flash.py`'s `TARGETS` mirrors `build.yaml`: when a build is added,
  renamed or gets an `artifact-name`, update both (and `--dry-run` it). The
  Keyboard Writer app (russwinch/keyboard-writer) has its own copy in
  `targets.json`; its ZMK module is pinned in `config/west.yml`.

## Flashing

The user flashes with their Keyboard Writer app (russwinch/keyboard-writer,
see README "Flashing"): it lists this repo's releases, one per build, and
flashes the devices in order. So when firmware is ready to try, don't spell
out `flash.py` commands. Say which release to pick in the app (`build-<run
number>`, and that it is a prerelease if it is a branch build), which devices
need it (a keymap or dongle-screen change only needs the central: the dongle,
or the left half without one) and whether a settings reset is needed.

### With `tools/flash.py`

Only when the user asks for it, and only possible when Claude runs on the
user's own computer: a cloud session can't see USB drives.

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
6. After a `--reset` on a `-dongle` keyboard, remind them: dongle stays powered,
   and re-pair the computer (forget the keyboard there first).
