#!/usr/bin/env python3
import shutil
import subprocess
import sys

APP = ["flatpak-spawn", "--host", "/usr/libexec/openkinect-v2/openkinect-v2-hostctl.sh"]


def run(args):
    if shutil.which("flatpak-spawn") is None:
        print("flatpak-spawn is required", file=sys.stderr)
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
