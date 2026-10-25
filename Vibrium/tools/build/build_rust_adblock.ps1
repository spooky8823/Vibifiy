$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$RustRoot = Join-Path $ProjectRoot "rust"
$CrateRoot = Join-Path $RustRoot "vibrium-adblock"
$HeaderPath = Join-Path $CrateRoot "include\vibrium_adblock.h"

Write-Host ""
Write-Host "=========================================="
Write-Host "       VIBRIUM RUST ADBLOCK BUILD"
Write-Host "=========================================="
Write-Host ""

if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    throw "Cargo was not found. Install Rust and ensure cargo is on PATH."
}

if (-not (Test-Path $HeaderPath)) {
    throw "C header not found: $HeaderPath"
}

Push-Location $RustRoot

try {
    Write-Host "Checking Rust formatting..."
    cargo fmt --all -- --check
    if ($LASTEXITCODE -ne 0) {
        throw "Rust formatting check failed."
    }

    Write-Host ""
    Write-Host "Running Rust tests..."
    cargo test --workspace
    if ($LASTEXITCODE -ne 0) {
        throw "Rust tests failed."
    }

    Write-Host ""
    Write-Host "Building release static library..."
    cargo build --release -p vibrium-adblock
    if ($LASTEXITCODE -ne 0) {
        throw "Rust release build failed."
    }
}
finally {
    Pop-Location
}

$LibraryPath = Join-Path $RustRoot "target\release\vibrium_adblock.lib"

if (-not (Test-Path $LibraryPath)) {
    throw "Expected static library was not produced: $LibraryPath"
}

Write-Host ""
Write-Host "Build completed."
Write-Host "Header:  $HeaderPath"
Write-Host "Library: $LibraryPath"
Write-Host ""
Write-Host "Note: This builds Rust only. Chromium GN linkage is not configured yet."
