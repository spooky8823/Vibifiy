from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "rust" / "vibrium-adblock" / "Cargo.toml"


def run(command: list[str], *, env: dict[str, str]) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build Vibrium's Rust ad-blocking static library."
    )
    parser.add_argument(
        "--target",
        help="Optional Rust target triple, such as x86_64-pc-windows-msvc.",
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Build the optimized release library.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        help="Optional directory for the resulting library.",
    )
    args = parser.parse_args()

    if not MANIFEST.is_file():
        print(f"ERROR: Cargo manifest not found: {MANIFEST}", file=sys.stderr)
        return 2

    cargo = shutil.which("cargo")
    if not cargo:
        print("ERROR: cargo was not found on PATH.", file=sys.stderr)
        return 2

    rustc = shutil.which("rustc")
    if not rustc:
        print("ERROR: rustc was not found on PATH.", file=sys.stderr)
        return 2

    env = os.environ.copy()
    command = [cargo, "build", "--manifest-path", str(MANIFEST)]

    if args.release:
        command.append("--release")

    if args.target:
        command.extend(["--target", args.target])

    try:
        print("===== Rust compiler =====", flush=True)
        run([rustc, "--version"], env=env)

        print("===== Cargo version =====", flush=True)
        run([cargo, "--version"], env=env)

        print("===== Building Vibrium ad-blocker =====", flush=True)
        run(command, env=env)

        profile = "release" if args.release else "debug"
        target_dir = ROOT / "rust" / "target"

        if args.target:
            target_dir = target_dir / args.target

        library_dir = target_dir / profile

        candidates = [
            library_dir / "vibrium_adblock.lib",
            library_dir / "libvibrium_adblock.a",
        ]

        library = next((path for path in candidates if path.is_file()), None)
        if library is None:
            print(
                "ERROR: Cargo succeeded, but the expected static library "
                f"was not found in {library_dir}.",
                file=sys.stderr,
            )
            return 1

        if args.out_dir:
            destination_dir = args.out_dir.resolve()
            destination_dir.mkdir(parents=True, exist_ok=True)
            destination = destination_dir / library.name
            shutil.copy2(library, destination)
            library = destination

        print(f"SUCCESS: {library}", flush=True)
        return 0

    except subprocess.CalledProcessError as error:
        print(
            f"ERROR: Build command exited with status {error.returncode}.",
            file=sys.stderr,
        )
        return error.returncode or 1


if __name__ == "__main__":
    raise SystemExit(main())
