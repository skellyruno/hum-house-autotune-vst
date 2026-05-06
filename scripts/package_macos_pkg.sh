#!/usr/bin/env bash
# Build a guided .pkg installer for macOS with EULA welcome screen.
# Installs VST3 + AU (+ Standalone) into system plug-in paths.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
ARTEFACTS="$BUILD_DIR/HumHouseVocalTune_artefacts/Release"
PKG_OUT="${PKG_OUT:-HumHouse-VocalTune-macOS.pkg}"
PKG_ID="com.humhouse.humhousevocaltune.pkg"
PKG_VERSION="1.0.0"

STAGING="$(mktemp -d)"
PAYLOAD="$STAGING/payload"
SCRIPTS="$STAGING/scripts"
RESOURCES="$STAGING/resources"

mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3" \
         "$PAYLOAD/Library/Audio/Plug-Ins/Components" \
         "$PAYLOAD/Applications" \
         "$SCRIPTS" \
         "$RESOURCES"

# Copy artefacts
cp -R "$ARTEFACTS/VST3/HumHouse Vocal Tune.vst3" "$PAYLOAD/Library/Audio/Plug-Ins/VST3/"

if [[ -d "$ARTEFACTS/AU/HumHouse Vocal Tune.component" ]]; then
  cp -R "$ARTEFACTS/AU/HumHouse Vocal Tune.component" "$PAYLOAD/Library/Audio/Plug-Ins/Components/"
fi

if [[ -d "$ARTEFACTS/Standalone/HumHouse Vocal Tune.app" ]]; then
  cp -R "$ARTEFACTS/Standalone/HumHouse Vocal Tune.app" "$PAYLOAD/Applications/"
fi

# EULA / welcome
cp installer/eula.txt "$RESOURCES/license.txt"

cat > "$RESOURCES/welcome.txt" <<'EOF'
Welcome to the HumHouse Vocal Tune installer.

This will install:
  - HumHouse Vocal Tune.vst3 (for all DAWs)
  - HumHouse Vocal Tune.component (for Logic Pro / GarageBand)
  - HumHouse Vocal Tune.app (Standalone)

Click Continue to proceed.
EOF

# Build component .pkg
pkgbuild --root "$PAYLOAD" \
         --identifier "$PKG_ID" \
         --version "$PKG_VERSION" \
         --install-location "/" \
         "$STAGING/component.pkg"

# Build product .pkg with EULA
cat > "$STAGING/distribution.xml" <<DISTXML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>HumHouse Vocal Tune</title>
    <license file="license.txt" />
    <welcome file="welcome.txt" />
    <pkg-ref id="$PKG_ID"/>
    <options customize="never" require-scripts="false"/>
    <choices-outline>
        <line choice="default">
            <line choice="$PKG_ID"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$PKG_ID" visible="false">
        <pkg-ref id="$PKG_ID"/>
    </choice>
    <pkg-ref id="$PKG_ID" version="$PKG_VERSION">component.pkg</pkg-ref>
</installer-gui-script>
DISTXML

productbuild --distribution "$STAGING/distribution.xml" \
             --resources "$RESOURCES" \
             --package-path "$STAGING" \
             "$PKG_OUT"

# Optional signing
if [[ -n "${APPLE_INSTALLER_ID:-}" ]]; then
  productsign --sign "$APPLE_INSTALLER_ID" "$PKG_OUT" "${PKG_OUT}.signed"
  mv "${PKG_OUT}.signed" "$PKG_OUT"
fi

echo "Installer created: $PKG_OUT"
