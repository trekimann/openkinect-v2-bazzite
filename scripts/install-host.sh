#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
PREFIX="${PREFIX:-/usr}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake_args=(
	-S "$REPO_ROOT"
	-B "$REPO_ROOT/$BUILD_DIR"
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_INSTALL_PREFIX="$PREFIX"
)

if [[ -n "$CMAKE_GENERATOR" ]]; then
	cmake_args+=( -G "$CMAKE_GENERATOR" )
fi

cmake "${cmake_args[@]}"
cmake --build "$REPO_ROOT/$BUILD_DIR"
sudo cmake --install "$REPO_ROOT/$BUILD_DIR"
sudo systemctl daemon-reload
systemctl --user daemon-reload || true

echo "Installed OpenKinect v2 host files to $PREFIX"
echo "Edit /etc/openkinect-v2/openkinect-v2.conf to enable IR/depth streams."
echo "Enable with: sudo systemctl enable --now openkinect-v2.service"
echo "Enable audio with: systemctl --user enable --now openkinect-audio.service"
