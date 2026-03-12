#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
SCRIPTS_DIR="$PROJECT_DIR/scripts"
VERSION="0.4.3"
PKG_NAME="CollabSync-$VERSION.pkg"

echo "==> Gathering plugin bundles..."
VST3_SRC="/Library/Audio/Plug-Ins/VST3/CollabSync.vst3"
AU_SRC="/Library/Audio/Plug-Ins/Components/CollabSync.component"

if [ ! -e "$VST3_SRC" ]; then
    echo "ERROR: $VST3_SRC not found. Run 'sudo cmake --build build' first."
    exit 1
fi

# Staging directory — mirrors the filesystem layout the pkg will install
STAGING="$DIST_DIR/staging"
rm -rf "$STAGING"
mkdir -p "$STAGING/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGING/Library/Audio/Plug-Ins/Components"

cp -r "$VST3_SRC" "$STAGING/Library/Audio/Plug-Ins/VST3/"
[ -e "$AU_SRC" ] && cp -r "$AU_SRC" "$STAGING/Library/Audio/Plug-Ins/Components/"

echo "==> Building component package..."
mkdir -p "$DIST_DIR"

pkgbuild \
    --root "$STAGING" \
    --scripts "$SCRIPTS_DIR" \
    --identifier "com.collabsync.plugin" \
    --version "$VERSION" \
    --install-location "/" \
    "$DIST_DIR/CollabSync-component.pkg"

echo "==> Building product archive..."
# Create a distribution XML for a nicer installer UI
cat > "$DIST_DIR/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>CollabSync $VERSION</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="true"/>
    <domains enable_localSystem="true"/>
    <pkg-ref id="com.collabsync.plugin"/>
    <choices-outline>
        <line choice="com.collabsync.plugin"/>
    </choices-outline>
    <choice id="com.collabsync.plugin" title="CollabSync Plugin">
        <pkg-ref id="com.collabsync.plugin"/>
    </choice>
    <pkg-ref id="com.collabsync.plugin" version="$VERSION" onConclusion="none">CollabSync-component.pkg</pkg-ref>
</installer-gui-script>
EOF

# Welcome screen
cat > "$DIST_DIR/welcome.html" <<EOF
<html><body style="font-family: -apple-system; padding: 20px;">
<h2>CollabSync $VERSION</h2>
<p>Real-time collaborative music plugin for FL Studio and Logic Pro.</p>
<p>Installs:</p>
<ul>
    <li>CollabSync.vst3 -&gt; /Library/Audio/Plug-Ins/VST3/</li>
    <li>CollabSync.component -&gt; /Library/Audio/Plug-Ins/Components/</li>
</ul>
<p>After installing, rescan plugins in your DAW.</p>
</body></html>
EOF

productbuild \
    --distribution "$DIST_DIR/distribution.xml" \
    --package-path "$DIST_DIR" \
    --resources "$DIST_DIR" \
    "$DIST_DIR/$PKG_NAME"

# Clean up intermediates
rm -f "$DIST_DIR/CollabSync-component.pkg"
rm -f "$DIST_DIR/distribution.xml"
rm -f "$DIST_DIR/welcome.html"
rm -rf "$STAGING"

echo ""
echo "✓ Installer ready: $DIST_DIR/$PKG_NAME"
echo ""
echo "Note: Because this package is unsigned, users will need to"
echo "right-click the .pkg -> Open to bypass Gatekeeper on first launch."
