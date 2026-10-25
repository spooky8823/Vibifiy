from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REVISION_FILE = ROOT / "chromium" / "revision.txt"


def main() -> int:
    if not REVISION_FILE.is_file():
        print(f"ERROR: Missing file: {REVISION_FILE}")
        return 2

    values: dict[str, str] = {}

    for raw_line in REVISION_FILE.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()

        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()

    version = values.get("VERSION", "")
    revision = values.get("REVISION", "")

    if not re.fullmatch(r"\d+(?:\.\d+){3}", version):
        print("ERROR: VERSION must be a full version such as 156.0.8064.2.")
        return 1

    if not re.fullmatch(r"[0-9a-fA-F]{40}", revision):
        print(f"Chromium version: {version}")
        print("Revision status: NOT PINNED")
        print("A 40-character Git commit hash is required.")
        return 1

    if values.get("STATUS") != "PINNED":
        print("ERROR: STATUS must be PINNED.")
        return 1

    print(f"Chromium version: {version}")
    print(f"Chromium revision: {revision.lower()}")
    print("Revision format is valid.")
    print("Note: remote commit existence and version-to-commit correspondence must be verified separately.")
    return 0


if __name__ == "__main__":
    sys.exit(main())