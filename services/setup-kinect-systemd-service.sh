#!/bin/bash
set -euo pipefail

PREFIX="${PREFIX:-/usr}"
LIBEXECDIR="${LIBEXECDIR:-$PREFIX/libexec/openkinect-v2}"
SYSTEMD_DIR="${SYSTEMD_DIR:-/etc/systemd/system}"
CONFIG_DIR="${CONFIG_DIR:-/etc/openkinect-v2}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sudo install -d "$SYSTEMD_DIR" "$CONFIG_DIR" "$LIBEXECDIR"
sudo install -m 0644 "$REPO_ROOT/services/openkinect-v2.service" "$SYSTEMD_DIR/openkinect-v2.service"
sudo install -m 0644 "$REPO_ROOT/services/openkinect-v2.conf" "$CONFIG_DIR/openkinect-v2.conf"
sudo install -m 0755 "$REPO_ROOT/camera/openkinect-v2-runner.sh" "$LIBEXECDIR/openkinect-v2-runner.sh"

echo "Installed service files. Build and install openkinect-v2d before starting the service."
sudo systemctl daemon-reload
sudo systemctl enable openkinect-v2.service

echo "Commands:"
echo "  sudo systemctl start openkinect-v2"
echo "  sudo systemctl status openkinect-v2"
echo "  sudo journalctl -u openkinect-v2 -f"
