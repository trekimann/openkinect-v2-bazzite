#!/usr/bin/env python3
import shutil
import subprocess
import sys
import os

HOSTCTL_CANDIDATES = [
    os.environ.get("OPENKINECT_HOSTCTL"),
    "/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh",
    "/usr/libexec/openkinect-v2/openkinect-v2-hostctl.sh",
]


def resolve_hostctl():
    for candidate in HOSTCTL_CANDIDATES:
        if not candidate:
            continue
        check = subprocess.run(
            ["flatpak-spawn", "--host", "test", "-x", candidate],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if check.returncode == 0:
            return candidate
    return HOSTCTL_CANDIDATES[-1]


def run(args):
    if shutil.which("flatpak-spawn") is None:
        print("flatpak-spawn is required; run this command from the Flatpak launcher or a Flatpak runtime.", file=sys.stderr)
        return 1
    return subprocess.call(["flatpak-spawn", "--host", resolve_hostctl()] + args)


def main():
    if len(sys.argv) == 1:
        code = run(["status"])
        if code == 0:
            print()
            return run(["audio-status"])
        return code
    return run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
