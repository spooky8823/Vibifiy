#!/usr/bin/env bash
# build_linux.sh  –  Build VibiPass for Linux
# Produces:
#   dist/VibiPass_linux.tar.gz   (portable, runs on any distro)
#   dist/vibipass_1.0_amd64.deb  (Debian/Ubuntu/Mint installer)
#
# Requirements:
#   pip install pyinstaller pywebview
#   sudo apt install python3-gi gir1.2-webkit2-4.0   # GTK WebKit2

set -e
cd "$(dirname "$0")"

echo "==> Installing Python deps..."
pip install -r requirements.txt --break-system-packages -q
pip install pyinstaller --break-system-packages -q

echo "==> Building with PyInstaller..."
pyinstaller vibipass.spec --clean --noconfirm

echo "==> Creating portable tarball..."
cd dist
tar -czf VibiPass_linux.tar.gz VibiPass/
cd ..

echo ""
echo "==> Building .deb package..."
VERSION="1.0"
ARCH="amd64"
PKG="vibipass_${VERSION}_${ARCH}"
mkdir -p "dist/${PKG}/DEBIAN"
mkdir -p "dist/${PKG}/usr/local/bin"
mkdir -p "dist/${PKG}/usr/local/lib/vibipass"
mkdir -p "dist/${PKG}/usr/share/applications"
mkdir -p "dist/${PKG}/usr/share/pixmaps"

# Copy app files
cp -r dist/VibiPass/* "dist/${PKG}/usr/local/lib/vibipass/"

# Launcher script
cat > "dist/${PKG}/usr/local/bin/vibipass" << 'EOF'
#!/bin/bash
exec /usr/local/lib/vibipass/VibiPass "$@"
EOF
chmod +x "dist/${PKG}/usr/local/bin/vibipass"

# Desktop entry
cat > "dist/${PKG}/usr/share/applications/vibipass.desktop" << 'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=VibiPass
Comment=Your personal secure password vault
Exec=/usr/local/bin/vibipass
Icon=vibipass
Terminal=false
Categories=Utility;Security;
Keywords=password;vault;secure;encryption;
StartupWMClass=VibiPass
EOF

# Copy icon
cp assets/theme/Red/credentials.png "dist/${PKG}/usr/share/pixmaps/vibipass.png" 2>/dev/null || true

# Control file
cat > "dist/${PKG}/DEBIAN/control" << EOF
Package: vibipass
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Depends: python3, gir1.2-webkit2-4.0 | gir1.2-webkit2-4.1, libgtk-3-0
Maintainer: VibiPass <vibipass@local>
Description: VibiPass - Secure Password Manager
 A local, encrypted password vault with a beautiful UI.
 All data is stored locally, encrypted with AES-256-GCM
 and PBKDF2 (100,000 iterations). Never uploaded anywhere.
EOF

# Post-install script
cat > "dist/${PKG}/DEBIAN/postinst" << 'EOF'
#!/bin/bash
update-desktop-database /usr/share/applications 2>/dev/null || true
EOF
chmod 755 "dist/${PKG}/DEBIAN/postinst"

dpkg-deb --build "dist/${PKG}" "dist/${PKG}.deb" 2>/dev/null || {
    echo "  Note: dpkg-deb not found – skipping .deb build."
    echo "  Install with: sudo apt install dpkg"
}

echo ""
echo "✅ Done! Outputs in dist/:"
ls -lh dist/*.tar.gz dist/*.deb 2>/dev/null || ls -lh dist/
echo ""
echo "Install .deb with:  sudo dpkg -i dist/${PKG}.deb"
echo "Or run portable:    ./dist/VibiPass/VibiPass"
