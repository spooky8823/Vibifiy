from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "rust" / "vibrium-adblock" / "Cargo.toml"


def main() -> int:
    if not MANIFEST.is_file():
        print(f"ERROR: Missing manifest: {MANIFEST}", file=sys.stderr)
        return 2

    text = MANIFEST.read_text(encoding="utf-8-sig")
    section = ""
    dependencies: list[tuple[str, str]] = []

    for raw_line in text.splitlines():
        line = raw_line.split("#", 1)[0].strip()

        if not line:
            continue

        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue

        if section not in {
            "dependencies",
            "dev-dependencies",
            "build-dependencies",
        }:
            continue

        match = re.match(r'([A-Za-z0-9_-]+)\s*=\s*(.+)', line)
        if match:
            name, declaration = match.groups()
            dependencies.append((section, name))

    print("Vibrium Rust dependency inventory")
    print(f"Manifest: {MANIFEST.relative_to(ROOT)}")
    print()

    if not dependencies:
        print("No dependencies found in the supported manifest sections.")
        return 0

    for section, name in dependencies:
        print(f"{section}: {name}")

    print()
    print("IMPORTANT:")
    print("- This inventory does not prove a Chromium GN target exists.")
    print("- Cargo path dependencies need their own source and build integration.")
    print("- Each crates.io dependency must be mapped to a compatible Chromium")
    print("  third-party Rust target or deliberately integrated another way.")
    print("- Do not add guessed GN labels to production BUILD.gn files.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())