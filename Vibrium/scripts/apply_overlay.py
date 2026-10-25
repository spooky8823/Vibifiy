from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ROOT / "chromium" / "overlay"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply Vibrium overlay files to a Chromium source checkout."
    )
    parser.add_argument(
        "--chromium",
        type=Path,
        required=True,
        help="Path to an existing Chromium source checkout.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show planned file operations without copying anything.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Allow replacing existing Chromium files.",
    )
    args = parser.parse_args()

    chromium = args.chromium.expanduser().resolve()

    if not OVERLAY.is_dir():
        print(f"ERROR: Overlay directory not found: {OVERLAY}", file=sys.stderr)
        return 2

    if not (chromium / "DEPS").is_file() or not (chromium / "BUILD.gn").is_file():
        print(
            "ERROR: Target does not look like a Chromium source checkout "
            "(expected DEPS and BUILD.gn).",
            file=sys.stderr,
        )
        return 2

    files = sorted(path for path in OVERLAY.rglob("*") if path.is_file())

    if not files:
        print("No overlay files found.")
        return 0

    conflicts = [
        source.relative_to(OVERLAY)
        for source in files
        if (chromium / source.relative_to(OVERLAY)).exists()
        and not args.overwrite
    ]

    if conflicts:
        print("ERROR: Existing target files would be overwritten:", file=sys.stderr)
        for relative in conflicts:
            print(f"  {relative}", file=sys.stderr)
        print("Review these files or rerun with --overwrite.", file=sys.stderr)
        return 1

    for source in files:
        relative = source.relative_to(OVERLAY)
        target = chromium / relative
        action = "REPLACE" if target.exists() else "CREATE"
        print(f"{action}: {relative}")

        if args.dry_run:
            continue

        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    if args.dry_run:
        print(f"Dry run complete: {len(files)} file(s) planned.")
    else:
        print(f"Overlay applied: {len(files)} file(s) copied.")

    print("Note: overlay copying does not register GN targets or apply patches.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())