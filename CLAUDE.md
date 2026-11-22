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
- Settings go in the widest file they are true for: `config/default.conf`
  (every build), `config/prospector_adapter.conf` (every dongle),
  `config/nice_view.conf` (every nice!view half), and only then
  `config/<keyboard>.conf`. Don't repeat a setting per board.
- The firmware only really gets checked by CI, so check the build run after
  pushing keymap changes.
- `keymap-drawer/keymap.svg` is redrawn and committed by the "Draw keymap"
  workflow; pull before pushing after a keymap change.
- Keep the README's tables (boards, layers, timings, encoders) in step with
  the keymap.
- Docs are for people: README.md (boards, keymap, flashing, the dongle),
  `src/screen_fx/README.md` (the dongle's screens), `tools/README.md`. Keep
  them short: what a user sees and does, not how it works or which file does
  it. Implementation notes worth keeping go below, or in code comments.
- The dongle's Debug screen (`debug_frame` in `src/screen_fx/fx.c`) is for
  seeing what works, what doesn't, and how the boards compare. When changing
  it:
  - keep like with like: in the link rows, the readings (dBm, %, mV), then the
    link's troubles (drops, reason, charging-state reads), then the counts;
  - give as much precision as the reading really has (mV, not volts to two
    places; hundredths of a mW) and show its unit, in the column header if
    there is no room beside the value;
  - prefer live readings to fixed facts, and say why one is missing (`--`
    not known, an amber or red code for why) rather than leaving it blank;
  - a reading that can stick or fail must only blank itself: anything that
    waits on hardware or a half goes on a thread of its own, never in the
    frame;
  - render it with `tools/fx_preview.py` and look before pushing, and keep
    the Debug tables in `src/screen_fx/README.md` in step.
- `tools/flash.py`'s `TARGETS` mirrors `build.yaml`: when a build is added,
  renamed or gets an `artifact-name`, update both (and `--dry-run` it). The
  Keyboard Writer app (russwinch/keyboard-writer) has its own copy in
  `targets.json`; its ZMK module is pinned in `config/west.yml`.

## Flashing

The user flashes with their Keyboard Writer app (russwinch/keyboard-writer,
see README, Flashing): it lists this repo's releases, one per build, and
flashes the devices in order. So when firmware is ready to try, don't spell
out `flash.py` commands. Say which release to pick in the app (`build-<run
number>`, and that it is a prerelease if it is a branch build), which devices
need it (a keymap or dongle-screen change only needs the central: the dongle,
or the left half without one) and whether a settings reset is needed.

### Settings-reset tags

From now on, when a commit needs a settings reset, tag it so the app turns its
reset on by itself (README, Flashing): a plain tag (not `git tag -a`, which
the app ignores) named `settings-reset/<why>`, `<why>` a few words joined by
hyphens that the app shows ("dongle-central", "more-bt-profiles"):

    git tag settings-reset/<why> <commit> && git push origin settings-reset/<why>

- A reset is needed when what a device keeps in flash no longer fits the new
  firmware: its split role changes (central to peripheral or back, e.g.
  moving to or from the dongle), a half or device is added to or removed from
  the split, a peripheral's identity or the BLE pairing/bonding settings
  change (`CONFIG_ZMK_SPLIT_BLE_*`, `CONFIG_BT_MAX_PAIRED`,
  `CONFIG_BT_MAX_CONN`, `CONFIG_ZMK_BLE_*` profiles), or the halves no longer
  pair. A keymap, behaviour or screen change does not.
- Tag the commit that makes the change, once it is pushed; every later build
  contains it. Only rebasing changes it: if the tagged commit gets rebased
  before reaching `main`, move the tag to the new one (`git tag -f`, then
  `git push -f origin settings-reset/<why>`).
- Pushing a tag builds nothing (`build.yml` only builds branches).
- Still say in the flashing summary that a reset is needed, and that the app
  will have switched it on.

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

## Implementation notes

- The dongle asks the halves for their charging state (`src/dongle_half_info.c`,
  sent by `src/half_charging.c` through the battery service's Battery Level
  Status) one request at a time, only after ZMK has finished setting the link
  up: ZMK can't wait for a free request while it subscribes to a half's keys,
  and when it couldn't, the half connected and showed its battery but typed
  nothing. Reading the halves' temperature or transmit power never worked, so
  don't add those back.
- Battery volts come from each half as the average of ZMK's last 16 readings;
  the draw (`src/dongle_half_drain.c`) fits a line through the energy left
  since unplugging, and the charging estimate adds `CONFIG_HALF_CHARGE_MA`
  over `CONFIG_HALF_BATTERY_MAH` (per keyboard `.conf`) to the last level
  before plugging in.
- `src/dongle_screen_fx.c` parks LVGL and writes frames straight to the panel;
  the animations' memory is one of LVGL's draw buffers, so only one animation
  runs at a time and a transition's other picture is the last frame. A whole
  frame takes about 70 ms over SPI.
- Crashes: the reason and program counter survive the restart in a register
  and the first KB of RAM (`config/prospector_adapter.overlay` keeps it out of
  Zephyr's). Each release has the dongles'
  `.elf`s in `dongle-elf.tar.gz`: `addr2line -ife <dongle>.elf 0x<address>`.
- Encoder knobs: `encoder_keys` and `ENCODER_TURN_SIGN` in
  `src/dongle_screen_fx.c`, in the order of the dongle overlay's `&sensors`.
- The animations place keys on a 10x4 grid (the Waterfowl); Keys, Status and
  Debug use the matrix transform.
- Themes: one line each in `src/screen_fx/colors.h`. Dark ones are on pure
  black with captions in the same strong hue; light ones adjust each colour's
  lightness, never its hue. Check `tools/fx_preview.py --contrast`.
