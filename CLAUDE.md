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
