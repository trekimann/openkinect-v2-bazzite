#!/bin/bash
set -euo pipefail

SCRIPT_PATH="$(realpath "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-host}"
LIBFREENECT2_DIR="${LIBFREENECT2_DIR:-$HOME/.cache/openkinect-v2/libfreenect2}"
LIBFREENECT2_BUILD_DIR="${LIBFREENECT2_BUILD_DIR:-$LIBFREENECT2_DIR/build}"
ENABLE_SERVICE="${OPENKINECT_ENABLE_SERVICE:-1}"
REBUILD_LIBFREENECT2="${OPENKINECT_REBUILD_LIBFREENECT2:-0}"
ENABLE_OPENCL="${OPENKINECT_ENABLE_OPENCL:-0}"

log() {
  printf '[openkinect-v2] %s\n' "$*"
}

die() {
  printf '[openkinect-v2] %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'USAGE'
Usage: scripts/install-bazzite-host.sh [--no-start] [--rebuild-libfreenect2]

Runs the Bazzite/Fedora host bootstrap flow.

- If started from a distrobox/dev container, the script re-runs itself on the host.
- On rpm-ostree systems, the first run may stage missing packages and ask for a reboot.
- After reboot, rerun the same command to build and install libfreenect2 and openkinect-v2.

Environment flags:
- `OPENKINECT_ENABLE_OPENCL=1` enables an experimental OpenCL build of libfreenect2.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-start)
      ENABLE_SERVICE=0
      ;;
    --rebuild-libfreenect2)
      REBUILD_LIBFREENECT2=1
      ;;
    --enable-opencl)
      ENABLE_OPENCL=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
  shift
done

if [[ -z "${OPENKINECT_RUNNING_ON_HOST:-}" ]] && [[ -f /run/.containerenv || -f /.dockerenv ]]; then
  if command -v distrobox-host-exec >/dev/null 2>&1; then
    log "Re-running installer on the Bazzite host via distrobox-host-exec"
    exec distrobox-host-exec env \
      OPENKINECT_RUNNING_ON_HOST=1 \
      OPENKINECT_ENABLE_SERVICE="$ENABLE_SERVICE" \
      OPENKINECT_REBUILD_LIBFREENECT2="$REBUILD_LIBFREENECT2" \
      OPENKINECT_ENABLE_OPENCL="$ENABLE_OPENCL" \
      BUILD_DIR="$BUILD_DIR" \
      LIBFREENECT2_DIR="$LIBFREENECT2_DIR" \
      LIBFREENECT2_BUILD_DIR="$LIBFREENECT2_BUILD_DIR" \
      bash "$SCRIPT_PATH"
  fi
  die "Container environment detected, but distrobox-host-exec is unavailable"
fi

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
else
  die "Unable to detect host operating system"
fi

if [[ "${ID:-}" != "bazzite" && "${ID:-}" != "fedora" && "${ID_LIKE:-}" != *fedora* ]]; then
  die "This bootstrap script currently supports Bazzite/Fedora hosts"
fi

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

apply_libfreenect2_opencl_compat_patch() {
  local source_file
  for source_file in \
    "$LIBFREENECT2_DIR/src/opencl_depth_packet_processor.cpp" \
    "$LIBFREENECT2_DIR/src/opencl_kde_depth_packet_processor.cpp"; do
    [[ -f "$source_file" ]] || continue

    if grep -q 'const int CL_ICDL_VERSION = 2;' "$source_file"; then
      sed -i \
        -e 's/const int CL_ICDL_VERSION = 2;/const int ocl_icdl_version_query = 2;/' \
        -e 's/clGetICDLoaderInfoOCLICD(CL_ICDL_VERSION,/clGetICDLoaderInfoOCLICD(ocl_icdl_version_query,/' \
        "$source_file"
    fi
  done
}

require_command git
require_command sudo
require_command bash

fedora_release="${VERSION_ID%%.*}"

rpmfusion_release_urls=(
  "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-${fedora_release}.noarch.rpm"
  "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-${fedora_release}.noarch.rpm"
)

ensure_rpmfusion() {
  local missing_rpmfusion=0
  if ! rpm -q rpmfusion-free-release >/dev/null 2>&1; then
    missing_rpmfusion=1
  fi
  if ! rpm -q rpmfusion-nonfree-release >/dev/null 2>&1; then
    missing_rpmfusion=1
  fi

  if [[ $missing_rpmfusion -eq 0 ]]; then
    return 0
  fi

  log "RPM Fusion release packages are missing on the host"
  if command -v rpm-ostree >/dev/null 2>&1; then
    log "Layering RPM Fusion release packages with rpm-ostree; sudo will prompt on the host"
    sudo rpm-ostree install "${rpmfusion_release_urls[@]}"
    log "Reboot the Bazzite host to activate RPM Fusion, then rerun this installer"
    exit 0
  fi

  require_command dnf
  log "Installing RPM Fusion release packages with dnf; sudo will prompt on the host"
  sudo dnf install -y "${rpmfusion_release_urls[@]}"
}

ensure_rpmfusion

host_packages=(
  akmod-v4l2loopback
  clinfo
  cmake
  gcc-c++
  git
  libusb1-devel
  make
  ninja-build
  pkgconf-pkg-config
  turbojpeg-devel
)

if [[ "$ENABLE_OPENCL" -eq 1 ]]; then
  host_packages+=(
    ocl-icd-devel.x86_64
    opencl-headers.noarch
  )
fi

missing_packages=()
for package in "${host_packages[@]}"; do
  if ! rpm -q "$package" >/dev/null 2>&1; then
    missing_packages+=("$package")
  fi
done

if [[ ${#missing_packages[@]} -gt 0 ]]; then
  log "Missing host packages: ${missing_packages[*]}"
  if command -v rpm-ostree >/dev/null 2>&1; then
    log "Staging host packages with rpm-ostree; sudo will prompt on the host"
    sudo rpm-ostree install "${missing_packages[@]}"
    log "Reboot the Bazzite host to apply the new deployment, then rerun this installer"
    exit 0
  fi

  require_command dnf
  log "Installing host packages with dnf; sudo will prompt on the host"
  sudo dnf install -y "${missing_packages[@]}"
fi

require_command cmake

if [[ "$REBUILD_LIBFREENECT2" -eq 1 || ! -f /usr/local/include/libfreenect2/libfreenect2.hpp || ! -e /usr/local/lib/libfreenect2.so ]]; then
  mkdir -p "$(dirname "$LIBFREENECT2_DIR")"
  if [[ -d "$LIBFREENECT2_DIR/.git" ]]; then
    log "Updating cached libfreenect2 source"
    git -C "$LIBFREENECT2_DIR" pull --ff-only
  else
    log "Cloning libfreenect2 into $LIBFREENECT2_DIR"
    rm -rf "$LIBFREENECT2_DIR"
    git clone --depth 1 https://github.com/OpenKinect/libfreenect2.git "$LIBFREENECT2_DIR"
  fi

  if [[ "$ENABLE_OPENCL" -eq 1 ]]; then
    log "Applying libfreenect2 OpenCL compatibility patch for current Fedora headers"
    apply_libfreenect2_opencl_compat_patch
  fi

  opencl_enabled=OFF
  if [[ "$ENABLE_OPENCL" -eq 1 ]]; then
    opencl_enabled=ON
  fi

  log "Configuring libfreenect2 build (OpenCL=${opencl_enabled})"
  cmake -S "$LIBFREENECT2_DIR" -B "$LIBFREENECT2_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_OPENNI2_DRIVER=OFF \
    -DENABLE_CXX11=ON \
    -DENABLE_OPENCL="$opencl_enabled" \
    -DENABLE_CUDA=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_VAAPI=OFF \
    -DENABLE_TEGRAJPEG=OFF

  log "Building libfreenect2"
  cmake --build "$LIBFREENECT2_BUILD_DIR"

  log "Installing libfreenect2 and Kinect udev rules; sudo will prompt on the host"
  sudo cmake --install "$LIBFREENECT2_BUILD_DIR"
  sudo install -Dm0644 "$LIBFREENECT2_DIR/platform/linux/udev/90-kinect2.rules" /etc/udev/rules.d/90-kinect2.rules
  sudo udevadm control --reload-rules
  sudo udevadm trigger
  sudo ldconfig
else
  log "Using existing libfreenect2 install on the host"
fi

log "Building and installing openkinect-v2 host files"
PREFIX=/usr/local BUILD_DIR="$BUILD_DIR" CMAKE_GENERATOR=Ninja "$REPO_ROOT/scripts/install-host.sh"

if [[ "$ENABLE_SERVICE" -eq 1 ]]; then
  log "Enabling and starting openkinect-v2.service; sudo will prompt on the host"
  sudo systemctl enable --now openkinect-v2.service
fi

log "Host installation complete"
log "Check service state with: sudo systemctl status openkinect-v2.service"
log "Review config at: /etc/openkinect-v2/openkinect-v2.conf"
if [[ "$ENABLE_OPENCL" -eq 1 ]]; then
  log "OpenCL build requested. Set PIPELINE=opencl in /etc/openkinect-v2/openkinect-v2.conf and restart the service to test GPU depth processing."
fi