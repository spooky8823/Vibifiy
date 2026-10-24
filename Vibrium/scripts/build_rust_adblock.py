from pathlib import Path
import subprocess
import sys


def main() -> int:
    project_root = Path(__file__).resolve().parents[1]
    rust_workspace = project_root / "rust"
    crate_manifest = rust_workspace / "vibrium-adblock" / "Cargo.toml"

    if not crate_manifest.is_file():
        print(f"ERROR: Rust crate manifest not found: {crate_manifest}", file=sys.stderr)
        return 1

    command = [
        "cargo",
        "build",
        "--manifest-path",
        str(crate_manifest),
        "-p",
        "vibrium-adblock",
        "--release",
    ]

    print("Building Vibrium Rust ad-blocking library...")
    print(f"Workspace: {rust_workspace}")

    try:
        subprocess.run(command, cwd=rust_workspace, check=True)
    except FileNotFoundError:
        print("ERROR: Cargo was not found. Install Rust and ensure cargo is on PATH.", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        print(f"ERROR: Cargo build failed with exit code {exc.returncode}.", file=sys.stderr)
        return exc.returncode or 1

    output_dir = rust_workspace / "target" / "release"
    candidates = [
        output_dir / "vibrium_adblock.lib",
        output_dir / "libvibrium_adblock.a",
    ]

    library = next((candidate for candidate in candidates if candidate.is_file()), None)

    if library is None:
        print(f"ERROR: Static library was not found in {output_dir}", file=sys.stderr)
        return 1

    print(f"SUCCESS: {library}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())