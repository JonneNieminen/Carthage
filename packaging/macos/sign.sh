#!/usr/bin/env bash
# Signs Carthage.app: ad-hoc by default, or with the certificate named by IDENTITY: Carthage's own
# self-signed certificate, or a Developer ID once the APPLE_* secrets exist (docs/RELEASING.md,
# "Signing and Gatekeeper"). Only a Developer ID gets the hardened runtime and a timestamp, which
# notarisation needs; a self-signed build is signed like an ad-hoc one, so plugins load the same.
# Inside-out, as Sparkle's documentation asks: Sparkle's helpers, the framework, carthage-scan, then
# the app.
#
# Usage: packaging/macos/sign.sh <Carthage.app> [IDENTITY]
set -euo pipefail

app="$1"
identity="${2:--}"
options=()
[[ "$identity" == "Developer ID Application"* ]] && options=(--options runtime --timestamp)

fw="$app/Contents/Frameworks/Sparkle.framework/Versions/B"
sign() { codesign --force --sign "$identity" ${options[@]+"${options[@]}"} "$@"; }

for helper in "$fw/XPCServices/Installer.xpc" "$fw/XPCServices/Downloader.xpc" "$fw/Autoupdate" "$fw/Updater.app"; do
  [[ -e "$helper" ]] && sign "$helper"
done
sign "$app/Contents/Frameworks/Sparkle.framework"
sign "$app/Contents/MacOS/carthage-scan"
sign "$app"

codesign --verify --deep --strict --verbose=1 "$app"
[[ "$identity" == "-" ]] && echo "Signed $app with an ad-hoc signature" || echo "Signed $app as \"$identity\""
