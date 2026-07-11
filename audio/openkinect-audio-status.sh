#!/bin/bash
set -euo pipefail

echo "=== OpenKinect v2 Audio Status ==="
DEVICE_NAME_PATTERN="Xbox NUI Sensor"

if arecord -l | grep -q "$DEVICE_NAME_PATTERN"; then
  CARD_NUM=$(arecord -l | sed -n "s/^card \\([0-9]\\+\\): ${DEVICE_NAME_PATTERN}.*/\\1/p" | head -1)
  echo "ALSA card: ${CARD_NUM:-unknown}"
else
  echo "ALSA card: not detected"
fi

if command -v pw-cli >/dev/null 2>&1; then
  NODE_NAME=$(pw-cli list-objects | grep -A5 "$DEVICE_NAME_PATTERN" | grep "node.name" | grep "alsa_input" | cut -d'"' -f2 | head -1 || true)
  echo "PipeWire node: ${NODE_NAME:-not detected}"
elif command -v pactl >/dev/null 2>&1; then
  SOURCE_NAME=$(pactl list sources | grep -B2 "$DEVICE_NAME_PATTERN" | grep "Name:" | awk '{print $2}' | head -1 || true)
  echo "PulseAudio source: ${SOURCE_NAME:-not detected}"
else
  echo "PipeWire/PulseAudio tools: unavailable"
fi
