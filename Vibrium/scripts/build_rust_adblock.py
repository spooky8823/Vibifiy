from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "rust" / "vibrium-adblock" / "Cargo.toml"
HEADER = ROOT / "rust" / "vibrium-adblock" / "include" / "vibrium_adblock.h"


def run(command: list[str], *, env: dict[str, str]) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build Vibrium's Rust ad-blocking static library."
    )
    parser.add_argument("--target", help="Optional Rust target triple.")
    parser.add_argument(
        "--release", action="store_true",
        help="Build the optimized release library.",
    )
    parser.add_argument(
        "--out-dir", type=Path,
        help="Optional directory for the resulting library.",
    )
    parser.add_argument(
        "--integration", action="store_true",
        help="Copy the library and C header to chromium/out/rust-adblock.",
    )
    args = parser.parse_args()

    if not MANIFEST.is_file():
        print(f"ERROR: Cargo manifest not found: {MANIFEST}", file=sys.stderr)
        return 2

    cargo = shutil.which("cargo")
    rustc = shutil.which("rustc")
    if not cargo or not rustc:
        print("ERROR: Both cargo and rustc must be available on PATH.", file=sys.stderr)
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
            target_dir /= args.target
        library_dir = target_dir / profile

        candidates = [
            library_dir / "vibrium_adblock.lib",
            library_dir / "libvibrium_adblock.a",
        ]
        library = next((p for p in candidates if p.is_file()), None)
        if library is None:
            print(
                f"ERROR: Rust build finished but no static library was found in {library_dir}",
                file=sys.stderr,
            )
            return 1

        destinations: list[Path] = []
        if args.out_dir:
            destinations.append(args.out_dir.resolve())
        if args.integration:
            destinations.append(ROOT / "chromium" / "out" / "rust-adblock")

        for destination_dir in destinations:
            destination_dir.mkdir(parents=True, exist_ok=True)
            destination = destination_dir / library.name
            shutil.copy2(library, destination)
            print(f"Library copied to: {destination}", flush=True)

            if args.integration:
                if not HEADER.is_file():
                    print(f"ERROR: FFI header not found: {HEADER}", file=sys.stderr)
                    return 2
                shutil.copy2(HEADER, destination_dir / HEADER.name)
                print(f"Header copied to: {destination_dir / HEADER.name}", flush=True)

        print(f"SUCCESS: {library}", flush=True)
        return 0

    except subprocess.CalledProcessError as error:
        print(f"ERROR: Build command exited with status {error.returncode}.", file=sys.stderr)
        return error.returncode or 1


if __name__ == "__main__":
    raise SystemExit(main())
