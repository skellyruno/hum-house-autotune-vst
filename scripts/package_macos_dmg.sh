#!/usr/bin/env bash
# Package the macOS build into a drag-to-install .dmg containing:
#   * HumHouse Vocal Tune.vst3   (Ableton, FL Studio, Reaper, Cubase, Studio One, etc.)
#   * HumHouse Vocal Tune.component (Logic Pro, GarageBand)
#   * HumHouse Vocal Tune.app    (Standalone)
#   * Symlinks to the system plug-in folders for drag & drop install
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
ARTEFACTS="$BUILD_DIR/HumHouseVocalTune_artefacts/Release"
VST3_SRC="$ARTEFACTS/VST3/HumHouse Vocal Tune.vst3"
AU_SRC="$ARTEFACTS/AU/HumHouse Vocal Tune.component"
APP_SRC="$ARTEFACTS/Standalone/HumHouse Vocal Tune.app"
STAGING="$(mktemp -d)/HumHouse Vocal Tune"
DMG_OUT="${DMG_OUT:-HumHouse-VocalTune-macOS.dmg}"

if [[ ! -d "$VST3_SRC" ]]; then
  echo "error: VST3 bundle not found at $VST3_SRC" >&2
  exit 1
fi

mkdir -p "$STAGING"
cp -R "$VST3_SRC" "$STAGING/"

if [[ -d "$AU_SRC" ]]; then
  cp -R "$AU_SRC" "$STAGING/"
fi

if [[ -d "$APP_SRC" ]]; then
  cp -R "$APP_SRC" "$STAGING/"
fi

# Drag-to-install targets
ln -s "/Library/Audio/Plug-Ins/VST3"      "$STAGING/VST3 Plug-Ins"
ln -s "/Library/Audio/Plug-Ins/Components" "$STAGING/Audio Unit Plug-Ins"

# README inside the DMG
cat > "$STAGING/README.txt" <<'EOF'
HumHouse Vocal Tune
====================

Dedicated pitch correction (autotune) plugin with thick vocal sustain,
sharp note snapping, and formant preservation.

INSTALL:
  1. Drag "HumHouse Vocal Tune.vst3" onto "VST3 Plug-Ins" folder.
  2. Drag "HumHouse Vocal Tune.component" onto "Audio Unit Plug-Ins" folder.
  3. Relaunch your DAW and rescan plugins.

STANDALONE:
  Double-click "HumHouse Vocal Tune.app" to run without a DAW.

If macOS Gatekeeper blocks the plugin:
  xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HumHouse Vocal Tune.vst3"
  xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HumHouse Vocal Tune.component"
EOF

# Optional codesign
if [[ -n "${APPLE_DEVELOPER_ID:-}" ]]; then
  echo "Signing with: $APPLE_DEVELOPER_ID"
  find "$STAGING" \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) -print0 |
    xargs -0 -I{} codesign --deep --force --options runtime \
      --sign "$APPLE_DEVELOPER_ID" "{}"
fi

# Create DMG
hdiutil create -volname "HumHouse Vocal Tune" \
  -srcfolder "$STAGING" \
  -ov -format UDZO \
  "$DMG_OUT"

echo "DMG created: $DMG_OUT"
