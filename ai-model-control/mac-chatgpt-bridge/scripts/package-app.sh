#!/bin/sh
set -eu

firmware=""
output=dist
if [ "$#" -gt 0 ] && [ "$1" = "--firmware" ]; then
    firmware="$2"
    if [ "$#" -gt 2 ]; then output="$3"; fi
elif [ "$#" -gt 0 ]; then
    output="$1"
fi

swift build -c release
bin="$(swift build -c release --show-bin-path)"
app="$output/Model Dial.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/Firmware"
install -m 755 "$bin/model-dial" "$app/Contents/MacOS/model-dial"
install -m 755 "$bin/chatgpt-bridge" "$app/Contents/MacOS/chatgpt-bridge"

plist="$app/Contents/Info.plist"
plutil -create xml1 "$plist"
plutil -insert CFBundleDisplayName -string "Model Dial" "$plist"
plutil -insert CFBundleExecutable -string "model-dial" "$plist"
plutil -insert CFBundleIdentifier -string "com.dachan.model-dial" "$plist"
plutil -insert CFBundleName -string "Model Dial" "$plist"
plutil -insert CFBundlePackageType -string "APPL" "$plist"
plutil -insert CFBundleShortVersionString -string "0.90" "$plist"
plutil -insert CFBundleVersion -string "1" "$plist"
plutil -insert LSMinimumSystemVersion -string "13.0" "$plist"
plutil -insert LSUIElement -bool true "$plist"

if [ -n "$firmware" ]; then
    install -m 644 "$firmware" "$app/Contents/Resources/Firmware/ai-model-control-v0.90.bin"
fi

echo "$app"
