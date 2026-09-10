#!/bin/sh
set -eu

firmware=""
output=dist
signing_identity="${MODEL_DIAL_SIGNING_IDENTITY:-}"
if [ "$#" -gt 0 ] && [ "$1" = "--firmware" ]; then
    firmware="$2"
    if [ "$#" -gt 2 ]; then output="$3"; fi
elif [ "$#" -gt 0 ]; then
    output="$1"
fi

swift build -c release
bin="$(swift build -c release --show-bin-path)"
app="$output/Model Dial.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/Firmware"
install -m 755 "$bin/model-dial" "$app/Contents/MacOS/model-dial"
install -m 755 "$bin/chatgpt-bridge" "$app/Contents/MacOS/chatgpt-bridge"

plist="$app/Contents/Info.plist"
if [ ! -f "$plist" ]; then plutil -create xml1 "$plist"; fi
plutil -replace CFBundleDisplayName -string "Model Dial" "$plist"
plutil -replace CFBundleExecutable -string "model-dial" "$plist"
plutil -replace CFBundleIdentifier -string "com.dachan.model-dial" "$plist"
plutil -replace CFBundleName -string "Model Dial" "$plist"
plutil -replace CFBundlePackageType -string "APPL" "$plist"
plutil -replace CFBundleShortVersionString -string "0.90" "$plist"
plutil -replace CFBundleVersion -string "1" "$plist"
plutil -replace LSMinimumSystemVersion -string "13.0" "$plist"
plutil -replace LSUIElement -bool true "$plist"

if [ -n "$firmware" ]; then
    install -m 644 "$firmware" "$app/Contents/Resources/Firmware/ai-model-control-v0.90.bin"
fi

if [ -n "$signing_identity" ]; then
    codesign --force --deep --options runtime --sign "$signing_identity" "$app"
fi

echo "$app"
