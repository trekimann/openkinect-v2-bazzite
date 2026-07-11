#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -f /run/.containerenv || -f /.dockerenv ]]; then
  exec "$REPO_ROOT/scripts/install-bazzite-host.sh" "$@"
fi

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${ID:-}" == "bazzite" || "${ID:-}" == "fedora" || "${ID_LIKE:-}" == *fedora* ]]; then
    exec "$REPO_ROOT/scripts/install-bazzite-host.sh" "$@"
  fi
fi

exec "$REPO_ROOT/scripts/install-host.sh" "$@"