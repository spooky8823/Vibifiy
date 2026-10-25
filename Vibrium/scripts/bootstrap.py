from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REVISION_FILE = ROOT / "chromium" / "revision.txt"
CHROMIUM_URL = "https://chromium.googlesource.com/chromium/src.git"


def load_revision() -> tuple[str, str]:
    if not REVISION_FILE.is_file():
        raise RuntimeError(f"Missing revision file: {REVISION_FILE}")

    values: dict[str, str] = {}

    for raw_line in REVISION_FILE.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()

        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()

    version = values.get("VERSION", "")
    revision = values.get("REVISION", "").lower()
    status = values.get("STATUS", "")

    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", version):
        raise RuntimeError("VERSION must be a full version such as 156.0.8064.2.")

    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise RuntimeError("REVISION must be a full 40-character Git commit hash.")

    if status != "PINNED":
        raise RuntimeError("STATUS must be PINNED.")

    return version, revision


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def require_tool(name: str) -> str:
    executable = shutil.which(name)

    if not executable:
        raise RuntimeError(
            f"Required tool '{name}' was not found on PATH. "
            "Install it in the build environment before continuing."
        )

    return executable


def verify_tag(git: str, version: str, revision: str) -> None:
    tag = f"{version}"
    result = subprocess.run(
        [
            git,
            "ls-remote",
            CHROMIUM_URL,
            f"refs/tags/{tag}",
            f"refs/tags/{tag}^{{}}",
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    hashes = {
        line.split()[0].lower()
        for line in result.stdout.splitlines()
        if line.strip()
    }

    if revision not in hashes:
        raise RuntimeError(
            f"The configured revision does not match the remote tag {tag}. "
            "Check chromium/revision.txt before building."
        )

    print(f"Verified remote tag {tag} against the configured revision.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare a pinned Chromium source checkout for Vibrium."
    )
    parser.add_argument(
        "--workspace",
        type=Path,
        required=True,
        help="Dedicated build workspace where Chromium source may be downloaded.",
    )
    parser.add_argument(
        "--sync-deps",
        action="store_true",
        help="Run gclient sync after checking out the pinned Chromium commit.",
    )
    parser.add_argument(
        "--allow-existing",
        action="store_true",
        help="Reuse an existing checkout only if it matches the pinned revision.",
    )
    args = parser.parse_args()

    try:
        version, revision = load_revision()

        workspace = args.workspace.expanduser().resolve()

        # Prevent accidentally placing Chromium source inside the Vibrium repo.
        if workspace == ROOT or ROOT in workspace.parents:
            raise RuntimeError(
                "Workspace must be outside the Vibrium project directory. "
                "Use a dedicated remote build workspace."
            )

        git = require_tool("git")
        fetch = shutil.which("fetch")
        gclient = shutil.which("gclient")

        if not fetch:
            raise RuntimeError(
                "Chromium depot_tools 'fetch' was not found on PATH. "
                "Install depot_tools in the build environment."
            )

        if args.sync_deps and not gclient:
            raise RuntimeError(
                "'gclient' was not found on PATH; it is required for dependency sync."
            )

        print(f"Vibrium project: {ROOT}")
        print(f"Chromium version: {version}")
        print(f"Chromium revision: {revision}")
        print(f"Build workspace: {workspace}")
        print("Chromium will not be downloaded unless bootstrap is executed.")

        verify_tag(git, version, revision)

        checkout = workspace / "chromium"
        workspace.mkdir(parents=True, exist_ok=True)

        if checkout.exists():
            if not args.allow_existing:
                raise RuntimeError(
                    f"Checkout path already exists: {checkout}\n"
                    "Use a fresh workspace, or pass --allow-existing to verify it."
                )

            if not (checkout / ".git").exists():
                raise RuntimeError(
                    f"Existing path is not a Git checkout: {checkout}"
                )

            current = subprocess.run(
                [git, "-C", str(checkout), "rev-parse", "HEAD"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip().lower()

            if current != revision:
                raise RuntimeError(
                    f"Existing checkout is at {current}, expected {revision}. "
                    "Use a fresh workspace."
                )
        else:
            env = os.environ.copy()
            run(
                [fetch, "--nohooks", "chromium"],
                cwd=workspace,
                env=env,
            )

            if not (checkout / ".git").exists():
                raise RuntimeError(
                    "depot_tools fetch completed, but the expected Chromium "
                    f"checkout was not found at {checkout}."
                )

            run(
                [git, "-C", str(checkout), "fetch", "origin", revision],
            )
            run(
                [git, "-C", str(checkout), "checkout", "--detach", revision],
            )

        if args.sync_deps:
            run(
                [gclient, "sync", "--no-history"],
                cwd=checkout,
            )

        print()
        print("Chromium checkout is pinned to the configured commit.")
        if args.sync_deps:
            print("Dependency synchronization completed.")
        else:
            print("Dependencies have NOT been synchronized.")
            print("Run again with --sync-deps in the remote build environment.")
        print("Vibrium overlays and patches have NOT been applied yet.")
        return 0

    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())