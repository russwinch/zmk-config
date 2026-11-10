#!/bin/sh
# Builds "ZMK Flasher.app" into tools/flasher/build/. Needs Xcode or its
# command line tools (xcode-select --install). Then:
#   open "tools/flasher/build/ZMK Flasher.app"
set -eu
cd "$(dirname "$0")"

swift build -c release
app="build/ZMK Flasher.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$(swift build -c release --show-bin-path)/Flasher" "$app/Contents/MacOS/Flasher"
cp ../targets.json "$app/Contents/Resources/targets.json"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>ZMK Flasher</string>
    <key>CFBundleDisplayName</key><string>ZMK Flasher</string>
    <key>CFBundleIdentifier</key><string>com.github.russwinch.zmk-flasher</string>
    <key>CFBundleExecutable</key><string>Flasher</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSDownloadsFolderUsageDescription</key><string>To find the firmware .zip you downloaded.</string>
    <key>NSRemovableVolumesUsageDescription</key><string>To copy firmware onto the keyboard's bootloader drive.</string>
</dict>
</plist>
PLIST
codesign --force --sign - "$app"
echo "Built $PWD/$app"
