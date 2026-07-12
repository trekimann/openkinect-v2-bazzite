#!/bin/bash
set -euo pipefail

CONFIG_FILE="${OPENKINECT_CONFIG:-/etc/openkinect-v2/openkinect-v2.conf}"
AUDIO_SERVICE="${OPENKINECT_AUDIO_SERVICE:-openkinect-audio.service}"
MODE="${1:-status}"
DEVICE_NAME_PATTERN="${OPENKINECT_AUDIO_MATCH:-Xbox NUI Sensor}"

show_pipewire_node() {
  if ! command -v pw-cli >/dev/null 2>&1; then
    echo "PipeWire node: unavailable (pw-cli not installed)"
    return
  fi

  local node_name
  node_name="$(pw-cli list-objects 2>/dev/null | grep -A5 "$DEVICE_NAME_PATTERN" | grep "node.name" | grep "alsa_input" | cut -d'"' -f2 | head -1 || true)"
  echo "PipeWire raw source: ${node_name:-not detected}"
}

if [[ "$MODE" == "status" ]]; then
  echo "=== OpenKinect v2 Audio Status ==="
  if command -v systemctl >/dev/null 2>&1; then
    echo "User service state: $(systemctl --user is-active "$AUDIO_SERVICE" 2>/dev/null || echo inactive)"
    echo "User service enabled: $(systemctl --user is-enabled "$AUDIO_SERVICE" 2>/dev/null || echo disabled)"
  fi
  show_pipewire_node
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required for openkinect-audio-status.sh" >&2
  exit 1
fi

python3 - "$CONFIG_FILE" "$MODE" <<'PY'
import json
import os
import sys

config_path = sys.argv[1]
mode = sys.argv[2]

config = {}
try:
    with open(config_path, encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            config[key.strip()] = value.strip().strip('"')
except FileNotFoundError:
    pass

state_path = config.get("AUDIO_STATE_PATH") or os.path.join(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
    "openkinect-v2",
    "audio-state.json",
)

try:
    with open(state_path, encoding="utf-8") as handle:
        state = json.load(handle)
except FileNotFoundError:
    state = None
except json.JSONDecodeError as exc:
    print(f"Audio state file is invalid ({state_path}): {exc}", file=sys.stderr)
    sys.exit(1)

if mode == "direction":
    if state is None:
        print(f"Audio direction metadata unavailable: {state_path}", file=sys.stderr)
        sys.exit(1)
    print(json.dumps(state, indent=2, sort_keys=True))
    sys.exit(0)

print(f"Audio state file: {state_path}")
print(f"Configured output mode: {config.get('AUDIO_OUTPUT_MODE', 'focused-mono')}")
print(f"Configured steering mode: {config.get('AUDIO_STEERING_MODE', 'auto')}")
if state is None:
    print("Runtime metadata: not available")
    sys.exit(0)

print(f"Effective output mode: {state.get('effective_output_mode', 'unknown')}")
print(f"Focused source enabled: {state.get('focused_source_enabled', False)}")
print(f"Azimuth: {state.get('azimuth_deg', 0.0)} deg")
print(f"Confidence: {state.get('confidence', 0.0)}")
print(f"Voice active: {state.get('voice_active', False)}")
print(f"Input RMS: {state.get('input_rms', 0.0)}")
print(f"Focused node name: {state.get('focused_node_name', '')}")
PY
