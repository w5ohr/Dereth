#!/bin/bash
# Builds build/dist/Open-WebUI-<version>.dmg from the built app: the app and an
# /Applications link in a compressed, read-only image, verified, mounted to
# check its contents, plus a filled-in Homebrew cask and the sha256.
#
#   scripts/make-dmg.sh [path/to/Open WebUI.app]
set -euo pipefail
cd "$(dirname "$0")/.."
APP="${1:-build/Open WebUI.app}"
[ -d "$APP" ] || { echo "no app at $APP - build first" >&2; exit 2; }
VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist")
DIST=build/dist
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/owui-dmg.XXXXXX")
DMG="$DIST/Open-WebUI-$VERSION.dmg"
mkdir -p "$DIST"
rm -f "$DMG"

# The image's contents: the app (copied with its signature) and a link to /Applications.
# Without extended attributes: a build inside a synced folder (~/Documents)
# carries Finder info and File Provider tags that strict verification rejects.
ditto --norsrc --noextattr --noacl "$APP" "$STAGE/Open WebUI.app"
xattr -cr "$STAGE/Open WebUI.app"
if ! codesign --verify --deep --strict "$STAGE/Open WebUI.app" 2>/dev/null; then
  codesign --force --deep --sign - "$STAGE/Open WebUI.app"   # ad hoc, as the build itself is
fi
codesign --verify --deep --strict "$STAGE/Open WebUI.app" || { echo "the app's signature does not verify" >&2; exit 3; }
ln -s /Applications "$STAGE/Applications"

hdiutil create -quiet -volname "Open WebUI $VERSION" -srcfolder "$STAGE" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$DMG"
rm -rf "$STAGE"
hdiutil verify -quiet "$DMG"

# Mount it read-only without Finder and check what a user would see.
MNT=$(mktemp -d "${TMPDIR:-/tmp}/owui-dmg-mnt.XXXXXX")
hdiutil attach -quiet -readonly -nobrowse -noautoopen -mountpoint "$MNT" "$DMG"
trap 'hdiutil detach -quiet "$MNT" 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true' EXIT
[ -x "$MNT/Open WebUI.app/Contents/MacOS/Open WebUI" ] || { echo "the app is missing from the image" >&2; exit 4; }
[ "$(readlink "$MNT/Applications")" = "/Applications" ] || { echo "the Applications link is missing" >&2; exit 4; }
MOUNTED_VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$MNT/Open WebUI.app/Contents/Info.plist")
[ "$MOUNTED_VERSION" = "$VERSION" ] || { echo "version mismatch inside the image" >&2; exit 4; }
# Not --strict here: hdiutil's copies carry com.apple.provenance, which macOS
# adds itself and a read-only image can't shed; strict mode calls that detritus,
# Gatekeeper doesn't (the staged bundle passed --strict above).
codesign --verify --deep "$MNT/Open WebUI.app"
"$MNT/Open WebUI.app/Contents/MacOS/Open WebUI" --pooltest > /dev/null || { echo "the app inside the image does not run" >&2; exit 5; }
hdiutil detach -quiet "$MNT"
rmdir "$MNT"
trap - EXIT

SHA=$(shasum -a 256 "$DMG" | cut -d' ' -f1)
sed -e "s/@VERSION@/$VERSION/" -e "s/@SHA256@/$SHA/" packaging/homebrew/open-webui-desktop.rb > "$DIST/open-webui-desktop.rb"
echo "$DMG"
echo "  version $VERSION, $(du -h "$DMG" | cut -f1 | tr -d ' '), sha256 $SHA"
echo "  contents checked: Open WebUI.app (signature verifies, runs) + Applications link"
echo "  cask: $DIST/open-webui-desktop.rb"
