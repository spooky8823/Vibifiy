#!/usr/bin/env bash
# build_macos.sh  –  Build VibiPass for macOS
# Produces:
#   dist/VibiPass.app          (.app bundle)
#   dist/VibiPass_1.0.dmg      (drag-to-install disk image)
#
# Requirements:
#   pip install pyinstaller pywebview
#   macOS 10.13+ with Xcode Command Line Tools

set -e
cd "$(dirname "$0")"

echo "==> Installing Python deps..."
pip install -r requirements.txt -q
pip install pyinstaller -q

echo "==> Building with PyInstaller..."
pyinstaller vibipass.spec --clean --noconfirm

echo "==> Fixing macOS app bundle permissions..."
chmod -R u+x dist/VibiPass.app/Contents/MacOS/ 2>/dev/null || true

echo "==> Creating DMG..."
DMG_NAME="VibiPass_1.0.dmg"
STAGING="dist/dmg_staging"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cp -r dist/VibiPass.app "$STAGING/"

# Create symlink to /Applications so users can drag & drop
ln -s /Applications "$STAGING/Applications"

# Create the DMG
hdiutil create \
    -volname "VibiPass" \
    -srcfolder "$STAGING" \
    -ov \
    -format UDZO \
    "dist/$DMG_NAME"

rm -rf "$STAGING"

echo ""
echo "✅ Done! Outputs:"
ls -lh dist/*.app dist/*.dmg 2>/dev/null || ls -lh dist/
echo ""
echo "Install: Open dist/$DMG_NAME and drag VibiPass.app to Applications"
