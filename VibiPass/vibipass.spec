# vibipass.spec
# Usage:
#   pyinstaller vibipass.spec
#
# Works on Linux, Windows, and macOS.

import sys, os
from PyInstaller.building.build_main import Analysis, PYZ, EXE, COLLECT, BUNDLE

block_cipher = None

a = Analysis(
    ['main.py'],
    pathex=['.'],
    binaries=[],
    datas=[
        ('html',   'html'),
        ('assets', 'assets'),
    ],
    hiddenimports=[
        'webview',
        'webview.platforms.gtk',      # Linux GTK backend
        'webview.platforms.winforms', # Windows backend
        'webview.platforms.cocoa',    # macOS backend
        'webview.platforms.qt',       # Qt fallback
        'clr',                        # pythonnet (Windows WebView2)
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

# ── Platform-specific settings ────────────────────────────────────────────────

IS_WIN = sys.platform == 'win32'
IS_MAC = sys.platform == 'darwin'

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='VibiPass',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,            # no terminal window
    icon='assets/vibipass.ico' if IS_WIN else ('assets/vibipass.icns' if IS_MAC else 'assets/vibipass.png'),
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='VibiPass',
)

# macOS: wrap in .app bundle
if IS_MAC:
    app = BUNDLE(
        coll,
        name='VibiPass.app',
        icon='assets/vibipass.icns',
        bundle_identifier='com.vibipass.app',
        info_plist={
            'NSHighResolutionCapable': True,
            'CFBundleShortVersionString': '1.0.0',
            'CFBundleName': 'VibiPass',
            'CFBundleDisplayName': 'VibiPass',
            'LSMinimumSystemVersion': '10.13.0',
            'NSHumanReadableCopyright': '© VibiPass',
        },
    )
