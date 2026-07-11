#!/usr/bin/env python3
import shutil
import subprocess
import sys
import os

HOSTCTL = os.environ.get(
    "OPENKINECT_HOSTCTL",
    "/usr/libexec/openkinect-v2/openkinect-v2-hostctl.sh",
)
APP = ["flatpak-spawn", "--host", HOSTCTL]


def run(args):
    if shutil.which("flatpak-spawn") is None:
        print("flatpak-spawn is required; run this command from the Flatpak launcher or a Flatpak runtime.", file=sys.stderr)
        return 1
    return subprocess.call(APP + args)


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
