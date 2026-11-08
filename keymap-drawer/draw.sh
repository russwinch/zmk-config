#!/usr/bin/env bash
# Draw every layer of config/keymap.dtsi to keymap-drawer/keymap.svg.
# Needs keymap-drawer: pip install keymap-drawer
# The Urchin keymap is used because it is the plain 34-key version of the
# shared layers; the other boards only add encoders and spare keys.
set -euo pipefail
cd "$(dirname "$0")/.."
keymap -c keymap-drawer/config.yaml parse -z config/urchin.keymap -c 10 \
    > keymap-drawer/keymap.yaml
keymap -c keymap-drawer/config.yaml draw keymap-drawer/keymap.yaml \
    --ortho-layout '{split: true, rows: 3, columns: 5, thumbs: 2}' \
    > keymap-drawer/keymap.svg
echo "wrote keymap-drawer/keymap.svg"
