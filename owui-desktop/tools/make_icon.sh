#!/bin/bash
# Rebuilds resources/AppIcon.icns from tools/make_icon.mm.
set -e
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
clang++ -std=c++20 -fobjc-arc -framework AppKit -framework CoreGraphics \
  -o "$tmp/make_icon" tools/make_icon.mm
mkdir -p "$tmp/AppIcon.iconset"
"$tmp/make_icon" "$tmp/AppIcon.iconset"
mkdir -p resources
iconutil -c icns "$tmp/AppIcon.iconset" -o resources/AppIcon.icns
echo "resources/AppIcon.icns updated"
