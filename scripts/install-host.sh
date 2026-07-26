#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
PREFIX="${PREFIX:-/usr}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_DIR="/etc/openkinect-v2"
CONFIG_FILE="$CONFIG_DIR/openkinect-v2.conf"
TEMPLATE_FILE="$PREFIX/share/openkinect-v2/openkinect-v2.conf.example"

merge_config_template() {
	local template_file="$1"
	local config_file="$2"

	if [[ ! -f "$template_file" ]]; then
		echo "Config template not found: $template_file" >&2
		return 1
	fi

	if [[ ! -f "$config_file" ]]; then
		sudo install -Dm0644 "$template_file" "$config_file"
		return 0
	fi

	while IFS= read -r line; do
		[[ -z "$line" || "$line" == \#* ]] && continue
		key="${line%%=*}"
		if ! sudo grep -q "^${key}=" "$config_file"; then
			echo "Adding missing config key: $key"
			printf '%s\n' "$line" | sudo tee -a "$config_file" >/dev/null
		fi
	done < "$template_file"
}

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
sudo install -d "$CONFIG_DIR"
merge_config_template "$TEMPLATE_FILE" "$CONFIG_FILE"
sudo systemctl daemon-reload

echo "Installed OpenKinect v2 host files to $PREFIX"
echo "Edit /etc/openkinect-v2/openkinect-v2.conf to enable IR/depth streams."
echo "Enable with: sudo systemctl enable --now openkinect-v2.service"
