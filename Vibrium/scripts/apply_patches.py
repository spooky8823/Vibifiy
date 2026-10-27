from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REVISION_FILE = ROOT / "chromium" / "revision.txt"
PATCH_DIR = ROOT / "chromium" / "patches"
PATCH_SERIES = PATCH_DIR / "series"


def load_revision() -> str:
    if not REVISION_FILE.is_file():
        raise RuntimeError(f"Missing revision file: {REVISION_FILE}")

    values: dict[str, str] = {}

    for raw_line in REVISION_FILE.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()

        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()

    revision = values.get("REVISION", "").lower()

    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise RuntimeError("REVISION must be a full 40-character Git commit hash.")

    if values.get("STATUS") != "PINNED":
        raise RuntimeError("Chromium revision STATUS must be PINNED.")

    return revision


def run(
    command: list[str],
    *,
    cwd: Path,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    print("+", subprocess.list2cmdline(command), flush=True)

    return subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        capture_output=capture_output,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate and apply Vibrium's ordered Chromium patch series."
    )
    parser.add_argument("--chromium", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    try:
        expected_revision = load_revision()
        chromium = args.chromium.expanduser().resolve()

        if not (chromium / ".git").exists():
            raise RuntimeError(f"Not a Git checkout: {chromium}")

        actual_revision = run(
            ["git", "rev-parse", "HEAD"],
            cwd=chromium,
            capture_output=True,
        ).stdout.strip().lower()

        if actual_revision != expected_revision:
            raise RuntimeError(
                f"Chromium HEAD is {actual_revision}, expected {expected_revision}."
            )

        status = run(
            ["git", "status", "--porcelain"],
            cwd=chromium,
            capture_output=True,
        ).stdout.strip()

        if status:
            raise RuntimeError(
                "Chromium checkout has local changes. Use a clean, dedicated checkout."
            )

        if not PATCH_SERIES.is_file():
            raise RuntimeError(f"Missing patch series: {PATCH_SERIES}")

        names = [
            line.strip()
            for line in PATCH_SERIES.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]

        if not names:
            raise RuntimeError("Patch series contains no patches.")

        patches: list[Path] = []

        for name in names:
            if Path(name).name != name or not name.endswith(".patch"):
                raise RuntimeError(f"Invalid patch filename: {name}")

            patch = PATCH_DIR / name

            if not patch.is_file() or patch.stat().st_size == 0:
                raise RuntimeError(f"Missing or empty patch: {patch}")

            patches.append(patch)

        # Validate every patch against the same pristine checkout.
        # Do not mutate Chromium until the complete series has passed.
        for patch in patches:
            run(
                [
                    "git",
                    "apply",
                    "--check",
                    "--",
                    str(patch),
                ],
                cwd=chromium,
            )

        print(f"All {len(patches)} patch(es) pass the initial applicability check.")

        if args.dry_run:
            print("Dry run complete. No files were modified.")
            return 0

        # Apply sequentially only after the initial checks pass.
        for patch in patches:
            run(
                [
                    "git",
                    "apply",
                    "--",
                    str(patch),
                ],
                cwd=chromium,
            )

        print(f"Successfully applied {len(patches)} patch(es).")
        return 0

    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
