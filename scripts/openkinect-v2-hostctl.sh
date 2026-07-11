#!/bin/bash
set -euo pipefail

SERVICE="${OPENKINECT_SERVICE:-openkinect-v2.service}"
AUDIO_STATUS_SCRIPT="${OPENKINECT_AUDIO_STATUS:-/usr/libexec/openkinect-v2/openkinect-audio-status.sh}"
CONFIG_FILE="${OPENKINECT_CONFIG:-/etc/openkinect-v2/openkinect-v2.conf}"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [start|stop|restart|status|logs|audio-status|config]
USAGE
}

command="${1:-status}"
case "$command" in
  start|stop|restart|status)
    exec systemctl "$command" "$SERVICE"
    ;;
  logs)
    exec journalctl -u "$SERVICE" -n "${2:-100}" --no-pager
    ;;
  audio-status)
    exec "$AUDIO_STATUS_SCRIPT"
    ;;
  config)
    exec cat "$CONFIG_FILE"
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac
