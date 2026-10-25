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
        raise RuntimeError("chromium/revision.txt needs a full 40-character REVISION.")

    if values.get("STATUS") != "PINNED":
        raise RuntimeError("Chromium revision STATUS must be PINNED.")

    return revision


def run(command: list[str], cwd: Path, dry_run: bool = False) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=cwd, check=True)


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

        actual_revision = subprocess.run(
            ["git", "-C", str(chromium), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip().lower()

        if actual_revision != expected_revision:
            raise RuntimeError(
                f"Chromium HEAD is {actual_revision}, expected {expected_revision}."
            )

        if not PATCH_SERIES.is_file():
            raise RuntimeError(f"Missing patch series: {PATCH_SERIES}")

        patch_names = [
            line.strip()
            for line in PATCH_SERIES.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]

        if not patch_names:
            print("Patch series is empty; nothing to apply.")
            return 0

        patches: list[Path] = []
        for name in patch_names:
            if Path(name).name != name or not name.endswith(".patch"):
                raise RuntimeError(f"Invalid patch entry in series: {name}")

            patch = PATCH_DIR / name
            if not patch.is_file():
                raise RuntimeError(f"Patch listed in series is missing: {patch}")

            patches.append(patch)

        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=chromium,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

        if status:
            raise RuntimeError(
                "Chromium checkout has local changes. Refusing to apply patches "
                "to avoid mixing with unrelated work. Use a clean, dedicated checkout."
            )

        for patch in patches:
            check = [
                "git",
                "apply",
                "--check",
                "--",
                str(patch),
            ]
            run(check, chromium, args.dry_run)

            apply = [
                "git",
                "apply",
                "--",
                str(patch),
            ]
            run(apply, chromium, args.dry_run)

        print(
            f"{'Dry run validated' if args.dry_run else 'Applied'} "
            f"{len(patches)} patch(es) against {expected_revision}."
        )
        return 0

    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())