#!/usr/bin/env bash
# Drag-to-install DMG from a signed Carthage.app: the app, an Applications shortcut and
# "Read me first.txt" with the one-time Gatekeeper step for builds without a Developer ID.
# The same DMG is the Sparkle update archive.
#
# Usage: packaging/macos/make-dmg.sh <Carthage.app> <version> <output-dir>
set -euo pipefail

src="$1"; version="$2"; out="$3"
here="$(cd "$(dirname "$0")" && pwd)"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
ditto "$src" "$stage/Carthage.app"
ln -s /Applications "$stage/Applications"
sed -e "s/{{VERSION}}/$version/g" "$here/Read me first.txt" > "$stage/Read me first.txt"

mkdir -p "$out"
dmg="$out/Carthage-$version-arm64.dmg"
rm -f "$dmg"
if diskutil image create from --help >/dev/null 2>&1; then
  diskutil image create from --format UDZO --volumeName "Carthage $version" "$stage" "$dmg" >/dev/null 2>&1
else # macOS before 26
  hdiutil create -volname "Carthage $version" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$dmg" >/dev/null
fi
echo "Created $dmg"
