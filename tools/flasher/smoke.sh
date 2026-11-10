#!/bin/sh
# CI check of the built app: runs a whole waterfowl-dongle reset against
# pretend bootloader drives and takes screenshots along the way.
#   tools/flasher/smoke.sh <folder for the screenshots>
set -eu
cd "$(dirname "$0")"
shots="$1"
work="$(mktemp -d)"
mkdir -p "$shots" "$work/firmware" "$work/volumes"
app="build/ZMK Flasher.app/Contents/MacOS/Flasher"

for name in "waterfowl_dongle prospector_adapter-xiao_ble__zmk-zmk" waterfowl_left_peripheral_for_dongle \
    waterfowl_right-nice_nano__zmk-zmk settings_reset-nice_nano__zmk-zmk settings_reset-xiao_ble__zmk-zmk; do
    echo "$name" > "$work/firmware/$name.uf2"
done
(cd "$work/firmware" && zip -q ../firmware.zip ./*.uf2)

shot() { screencapture -x "$shots/$1.png" || echo "no screenshot: $1"; }

# plug <drive name> <INFO_UF2.TXT text> <file expected>: a board that restarts once it has the file
plug() {
    mkdir "$work/volumes/$1"
    echo "$2" > "$work/volumes/$1/INFO_UF2.TXT"
    for _ in $(seq 100); do
        if [ -f "$work/volumes/$1/$3.uf2" ]; then
            rm -r "$work/volumes/$1"
            echo "flashed: $3"
            return
        fi
        sleep 0.2
    done
    echo "FAILED: $3 never arrived on $1" >&2
    exit 1
}

"$app" --firmware "$work/firmware.zip" --volumes "$work/volumes" --keyboard waterfowl-dongle --reset &
setup=$!
sleep 4
shot 1-setup
kill $setup

"$app" --firmware "$work/firmware.zip" --volumes "$work/volumes" --keyboard waterfowl-dongle --reset --start &
run=$!
trap 'kill $run 2>/dev/null || true' EXIT
sleep 4
shot 2-dongle
plug XIAO-SENSE "Model: Seeed XIAO nRF52840" settings_reset-xiao_ble__zmk-zmk
plug XIAO-SENSE "Model: Seeed XIAO nRF52840" "waterfowl_dongle prospector_adapter-xiao_ble__zmk-zmk"
sleep 2
shot 3-left

# the wrong board must be left alone
mkdir "$work/volumes/XIAO-SENSE"
echo "Model: Seeed XIAO nRF52840" > "$work/volumes/XIAO-SENSE/INFO_UF2.TXT"
sleep 2
shot 4-wrong-board
[ "$(ls "$work/volumes/XIAO-SENSE")" = "INFO_UF2.TXT" ] || { echo "FAILED: copied onto the wrong board" >&2; exit 1; }
rm -r "$work/volumes/XIAO-SENSE"

plug NICENANO "Model: nice!nano" settings_reset-nice_nano__zmk-zmk
sleep 2
shot 5-left-again
plug NICENANO "Model: nice!nano" waterfowl_left_peripheral_for_dongle
plug NICENANO "Model: nice!nano" settings_reset-nice_nano__zmk-zmk
plug NICENANO "Model: nice!nano" waterfowl_right-nice_nano__zmk-zmk
sleep 2
shot 6-finished
kill -0 $run || { echo "FAILED: the app is no longer running" >&2; exit 1; }
echo "smoke test passed"
