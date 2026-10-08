@echo off
REM build_windows.bat  –  Build VibiPass for Windows
REM Produces:
REM   dist\VibiPass_windows.zip          (portable)
REM   dist\VibiPass_Setup_1.0.exe        (installer via Inno Setup, optional)
REM
REM Requirements:
REM   pip install pyinstaller pywebview pythonnet
REM   (optional) Inno Setup 6 installed at default path

setlocal enabledelayedexpansion
cd /d "%~dp0"

echo =^> Installing Python deps...
pip install -r requirements.txt -q
pip install pyinstaller pythonnet -q

echo =^> Building with PyInstaller...
pyinstaller vibipass.spec --clean --noconfirm

echo =^> Creating portable ZIP...
powershell -Command "Compress-Archive -Path 'dist\VibiPass\*' -DestinationPath 'dist\VibiPass_windows.zip' -Force"

echo =^> Checking for Inno Setup...
set ISCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if exist %ISCC% (
    echo =^> Building installer with Inno Setup...
    %ISCC% vibipass_installer.iss
) else (
    echo    Inno Setup not found – skipping installer build.
    echo    Download from: https://jrsoftware.org/isdl.php
)

echo.
echo Done! Outputs in dist\
dir dist\
