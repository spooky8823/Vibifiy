from __future__ import annotations

import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCKFILE = ROOT / "rust" / "Cargo.lock"


def main() -> int:
    if not LOCKFILE.is_file():
        print(f"ERROR: Missing lockfile: {LOCKFILE}", file=sys.stderr)
        return 2

    try:
        data = tomllib.loads(LOCKFILE.read_text(encoding="utf-8-sig"))
    except (tomllib.TOMLDecodeError, OSError) as exc:
        print(f"ERROR: Could not read lockfile: {exc}", file=sys.stderr)
        return 2

    packages = data.get("package", [])
    packages.sort(key=lambda package: (package.get("name", ""), package.get("version", "")))

    print(f"Lockfile: {LOCKFILE.relative_to(ROOT)}")
    print(f"Locked packages: {len(packages)}")
    print()

    for package in packages:
        name = package.get("name", "<unknown>")
        version = package.get("version", "<unknown>")
        source = package.get("source", "local/path dependency")
        print(f"{name} {version} | {source}")

    print()
    print("This is the Cargo dependency inventory, not a Chromium GN target map.")
    print("The next step is to map these packages to the pinned Chromium checkout.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())