#!/usr/bin/env bash
# Signs Carthage.app: ad-hoc by default, or with a Developer ID when IDENTITY is given (the release
# workflow passes it once the APPLE_* secrets exist; docs/RELEASING.md, "Signing and Gatekeeper").
# Inside-out, as Sparkle's documentation asks: Sparkle's helpers, the framework, carthage-scan, then
# the app.
#
# Usage: packaging/macos/sign.sh <Carthage.app> [IDENTITY]
set -euo pipefail

app="$1"
identity="${2:--}"
options=()
[[ "$identity" != "-" ]] && options=(--options runtime --timestamp)

fw="$app/Contents/Frameworks/Sparkle.framework/Versions/B"
sign() { codesign --force --sign "$identity" ${options[@]+"${options[@]}"} "$@"; }

for helper in "$fw/XPCServices/Installer.xpc" "$fw/XPCServices/Downloader.xpc" "$fw/Autoupdate" "$fw/Updater.app"; do
  [[ -e "$helper" ]] && sign "$helper"
done
sign "$app/Contents/Frameworks/Sparkle.framework"
sign "$app/Contents/MacOS/carthage-scan"
sign "$app"

codesign --verify --deep --strict --verbose=1 "$app"
echo "Signed $app with ${identity/#-/an ad-hoc signature}"
