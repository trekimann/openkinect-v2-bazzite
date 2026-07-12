# Microphone Enhancement and Speaker Focus Implementation Plan

## Purpose

This document defines an implementation-ready plan for two related features:

1. **Directional microphone processing** for the Kinect v2 4-microphone array
2. **Speaker focus camera mode** that tracks the active speaker and publishes a separate digitally zoomed camera stream

The intent is to fit these features into the current repository architecture and Bazzite packaging model without replacing the existing color camera stream.

## Clarified product constraints

The current implementation plan should assume:

- **Bazzite only** for the first shipping target
- **as few new dependencies as possible**
- **depth-assisted speaker focus** should be part of the design
- microphone processing should support **multiple switchable output modes** over time
- speaker focus should behave as **auto-framing**, not a tight follow camera
- the architecture should remain friendly to **future GPU offload**, especially shader-based acceleration

## Current Repository Baseline

### What already exists

- `camera/kinect2v4l2.cpp` builds `openkinect-v2d`, the current host daemon for color, IR, and depth streaming through labeled V4L2 loopback devices.
- `camera/openkinect-v2-runner.sh.in` provisions one or more labeled `v4l2loopback` devices and launches `openkinect-v2d`.
- `services/openkinect-v2.conf` already controls stream enablement and stream labels.
- `scripts/openkinect-v2-hostctl.sh.in` and `packaging/flatpak/openkinect-control.py` already provide host/Flatpak control for mode switching and service status.
- The `audio/` directory currently contains helper scripts for discovery, recording, PipeWire inspection, and simple amplification, but no persistent real-time processing service.

### Important constraints from the current codebase

- The production runtime is currently **host-side and systemd-managed**.
- Bazzite support is built around a **host companion package** with a **thin Flatpak launcher**.
- Current camera mode handling is video-stream oriented (`color`, `ir`, `depth`, `all`), not audio-processing oriented.
- Existing `docs/BEAMFORMING-ROADMAP.md` is useful background, but it assumes a JACK/PulseAudio-centric path and does not cover speaker-focus video integration or the current Bazzite-first packaging model.

## Product Goals

### Microphone goals

- Use the Kinect v2 4-microphone array as a processed microphone system that can expose **raw**, **focused mono**, and later **processed stereo** outputs.
- Improve intelligibility relative to raw capture through beamforming, gain normalization, and optional post-processing.
- Support **automatic steering** toward the active speaker.
- Keep end-to-end latency low enough for video calls and live streaming.
- Keep output modes structured so they can later be switched from the planned Python GUI.

### Speaker focus goals

- Estimate the speaker direction relative to the camera.
- Publish a **separate camera output** for speaker focus instead of replacing `Kinect_Color`.
- Smoothly crop and digitally zoom toward the current speaker in multi-person scenarios such as a boardroom.
- Preserve manual access to the existing color, IR, and depth streams.
- Prefer **auto-framing behavior** that keeps the active speaker comfortably framed instead of aggressively tight tracking.

## Recommended Architecture

## 1. Add a dedicated host audio service

Implement microphone processing as a **separate native daemon** instead of folding it into `openkinect-v2d`.

### Why this is the better fit

- Audio DSP has different timing and buffering requirements than video streaming.
- A separate process isolates failures and simplifies debugging.
- Packaging and systemd integration remain consistent with the current host-companion approach.
- The existing video daemon can remain focused on sensor-to-V4L2 streaming.

### Proposed components

- `openkinect-audiod` — captures raw 4-channel Kinect audio and publishes processed outputs
- `openkinect-speakerd` or an equivalent speaker-focus worker — consumes speaker direction metadata plus camera/depth frames to produce the zoomed stream

If process count needs to stay lower, the speaker-focus worker can be merged into the video daemon later, but the initial plan should keep audio DSP isolated from video transport.

## 2. Standardize around PipeWire for runtime integration

The current repo and target environment already lean on PipeWire utilities. Production integration should therefore target **PipeWire on Bazzite only** for the initial shipping path, not JACK and not a general multi-distro abstraction layer.

### Runtime path

- Capture raw 4-channel audio from the Kinect ALSA/PipeWire source
- Process it in the host daemon
- Publish:
  - a **raw multi-channel source** for diagnostics and advanced use cases
  - a **processed focused mono source** for meetings and streaming
  - a future **processed stereo source**
  - **speaker direction metadata** for camera focus

### Why not make JACK the main runtime

- JACK adds operational complexity on Bazzite desktops.
- PipeWire can cover both desktop integration and low-latency routing.
- The existing helper scripts already assume PipeWire is present on many systems.

JACK can still be used for offline development or benchmarking, but it should not be the primary shipping architecture.

### Audio mode design requirement

The daemon and config model should be designed so audio outputs are selectable rather than hard-coded. The long-term mode set should include:

- raw 4-channel capture
- focused mono
- processed stereo

Those modes should be controllable later from the planned Python GUI, so the internal API and config format should expose named modes instead of assuming a single processed output.

## 3. Treat speaker direction as shared metadata

Do not hard-wire speaker focus logic directly into the audio output path. Instead, produce a small shared state object that other components can consume.

### Proposed metadata output

Write a state file or local socket under `/run/openkinect-v2/`, for example:

- current azimuth in degrees
- confidence score
- voice-activity state
- timestamp
- optional candidate speaker list later

This keeps the system modular:

- the processed microphone can work without speaker focus
- the speaker-focus camera can reuse the same metadata
- future GUIs can display direction status without invasive changes

## Detailed Implementation Phases

## Phase 0 - Discovery and calibration

### Goals

- Confirm the Kinect mic channel order and spacing assumptions used on Linux.
- Confirm whether per-channel gain mismatch or timing skew must be compensated in software.
- Establish baseline measurements for noise floor, latency, and raw capture level.

### Work items

- Add a repeatable capture script for 4-channel diagnostic recordings.
- Record test clips from known angles and distances.
- Verify the real channel ordering exposed through ALSA/PipeWire.
- Document array geometry assumptions in-repo.
- Create a small fixture set for future offline regression testing.

### Deliverables

- Validated microphone geometry/channel map document
- Sample multi-channel recordings for testing
- Baseline latency and level measurements

## Phase 1 - Audio service foundation

### Goals

- Introduce a production-ready native audio service.
- Keep repository conventions aligned with the existing CMake/systemd packaging flow.

### Work items

- Add a new build target in `CMakeLists.txt` for `openkinect-audiod`.
- Add a new service template such as `services/openkinect-audio.service.in`.
- Extend installation scripts to install and manage the audio service.
- Define `/etc/openkinect-v2/openkinect-v2.conf` keys for audio enablement and tuning.
- Add host control commands for audio status/start/stop/restart.

### Recommended implementation choice

Use **native C++** for the production daemon to match the current build and packaging model. Python can still be used for offline analysis or prototype notebooks, but it should not become a hard runtime dependency for the shipping path.

### Initial config additions

Add config keys for:

- `ENABLE_AUDIO`
- `AUDIO_BACKEND`
- `AUDIO_FRAME_MS`
- `AUDIO_AGC_ENABLE`
- `AUDIO_NOISE_REDUCTION_ENABLE`
- `AUDIO_BEAMFORM_MODE`
- `AUDIO_STEERING_MODE`
- `AUDIO_OUTPUT_MODE`
- `AUDIO_EXPOSE_RAW_SOURCE`
- `AUDIO_EXPOSE_STEREO_SOURCE`
- `AUDIO_EXPOSE_FOCUSED_MONO_SOURCE`

## Phase 2 - Beamforming MVP

### Goals

- Ship a first usable directional microphone with low algorithmic risk.
- Prioritize reliability over maximum rejection performance.

### Recommended first algorithm

Start with **delay-and-sum beamforming** in the speech band.

### Why this should be first

- Lower implementation complexity
- Easier to validate with recorded fixtures
- Good enough to establish steering, latency, and integration plumbing
- Creates a stable baseline for later MVDR/post-filter work

### Processing stages

- Per-channel DC removal/high-pass filtering
- Optional channel gain calibration
- Voice activity detection
- Delay estimation or steering input
- Delay-and-sum beamforming
- Output limiting/AGC
- Optional light noise suppression

### MVP output behavior

- Continue exposing raw multi-channel audio for diagnostics
- Expose processed mono audio as the default application-facing microphone
- Provide a manual steering mode for testing before auto-steering is enabled
- Structure the processing graph so stereo output can be added later without redesigning the daemon

## Phase 3 - Direction finding and auto-steering

### Goals

- Estimate active speaker direction in real time.
- Feed that direction into both beam steering and camera focus.

### Recommended first localization method

Use **GCC-PHAT-based time-difference-of-arrival estimation** with smoothing and confidence scoring.

### Why this is the right first step

- It is much simpler than MUSIC or other high-resolution methods.
- It works well enough for a linear array when the main requirement is speaker-facing beam steering.
- It provides a practical azimuth estimate that can later be fused with depth or vision data.

### Work items

- Compute frame-by-frame TDOA estimates from the 4 channels.
- Convert TDOA into azimuth estimates based on validated mic spacing.
- Add hysteresis and temporal smoothing to avoid rapid angle flipping.
- Gate updates on voice activity and confidence thresholds.
- Publish azimuth/confidence metadata under `/run/openkinect-v2/`.

### Known limitation

The Kinect v2 mic array is linear, so audio alone gives a strong **horizontal direction estimate** but limited elevation and distance information. Speaker focus should therefore combine audio with depth and/or visual cues instead of relying on audio only.

## Phase 4 - Speaker focus camera mode

### Goals

- Produce a new speaker-focused camera output while preserving the existing color stream.
- Use audio direction as the primary cue and depth/video as disambiguation inputs.

### Recommended behavior

- Keep `Kinect_Color` unchanged.
- Add a new labeled output such as `Kinect_SpeakerFocus`.
- Run speaker focus only when color is enabled.
- Strongly prefer depth as an input when speaker focus is enabled, even if depth is not exposed publicly.
- Make framing decisions for a boardroom-style **auto-framing** result rather than a narrow action-camera style crop.

### Why depth should be part of the plan

Audio azimuth alone does not identify which person in the scene is speaking when multiple people are along a similar angle. Depth gives a way to:

- reject background regions
- estimate person-sized foreground clusters
- stabilize crop targets
- improve zoom decisions in a boardroom layout

### MVP targeting strategy

1. Use audio azimuth to select a horizontal region of interest.
2. Use depth occupancy within that region to identify the most likely speaking subject.
3. Map the chosen target into an auto-framing crop window on the 1080p color frame.
4. Apply smoothing, zoom limits, framing margins, and transition hysteresis.
5. Publish the cropped frame to `Kinect_SpeakerFocus`.

### Initial speaker focus rules

- Never disable the base color stream when speaker focus is active.
- Limit zoom so image quality stays acceptable.
- Delay speaker handoff briefly to prevent rapid camera jumps.
- Fall back to a wider framing when no speaker is confidently detected.
- Bias framing toward stable medium shots rather than maximum zoom.

### Camera-mode changes

Extend host control and config semantics so speaker focus is a **separate output mode**, not a replacement mode. Possible approaches:

- add `speaker-focus` as a control mode that implies `ENABLE_COLOR=1` and internal depth access
- or add a dedicated `ENABLE_SPEAKER_FOCUS` flag independent of the color/IR/depth flags

The second option is cleaner because it preserves the current meaning of `color`, `ir`, `depth`, and `all`.

## Phase 5 - Advanced quality improvements

### Audio improvements

After the MVP is stable, add higher-value DSP enhancements in order:

1. Better AGC tuned for the Kinect's quiet raw capture
2. Adaptive post-filtering or spectral noise reduction
3. Optional MVDR/Capon beamforming mode
4. Better multi-speaker arbitration
5. Presets for desktop, couch, and boardroom layouts
6. Full stereo output mode

### Speaker focus improvements

- Add face or person detection for stronger target association
- Fuse color/depth detections with audio confidence
- Add configurable framing styles (tight, medium, wide)
- Expose live status to the future GUI/Flatpak control surface

## Repository Changes Required

### Build system

Update `CMakeLists.txt` to:

- build the new audio daemon
- install any new helper binaries/scripts
- install additional systemd units
- install updated configuration templates

### Configuration

Extend `services/openkinect-v2.conf` to include:

- audio enablement and tuning keys
- output-mode selection
- speaker focus enablement
- focus stream label
- zoom/framing/timing thresholds
- flags controlling which audio sources are exposed

### Services

Add service definitions such as:

- `services/openkinect-audio.service.in`
- optional `services/openkinect-speaker-focus.service.in` if the focus pipeline stays separate

### Host control

Extend `scripts/openkinect-v2-hostctl.sh.in` with commands for:

- audio lifecycle
- audio mode selection
- speaker focus enable/disable
- speaker direction status
- focus mode selection
- diagnostics output

### Flatpak launcher and future GUI path

Extend `packaging/flatpak/openkinect-control.py` so the launcher can forward:

- audio status
- audio mode selection
- speaker-focus status
- speaker-focus enable/disable
- future preset selection

The control surface should be kept compatible with a future Python GUI so that raw, focused mono, and stereo outputs can be switched without redesigning the host API.

### Documentation

Update or replace older roadmap material so the active plan reflects:

- PipeWire-first runtime assumptions
- Bazzite-only first shipping scope
- host-companion packaging
- separate speaker-focus output
- C++ production implementation path
- minimal-dependency expectations

## Dependency Strategy

### Prefer small native dependencies

The production path should prefer lightweight native libraries over a Python-heavy runtime stack.

The first implementation should add only the dependencies required for a Bazzite-first production path. If a feature can be built with existing system libraries or a very small native dependency, prefer that over introducing large frameworks.

### Good candidate dependencies

- **PipeWire development libraries** for native source/sink integration
- **FFTW** or a small FFT library for frequency-domain processing if needed
- **SpeexDSP** for AGC, resampling, or pre/post-processing support if needed

### Dependencies to defer unless truly needed

- full Python DSP stacks in the runtime path
- OpenCV-based speaker focus logic in the MVP
- heavy ML frameworks for voice or vision processing
- large cross-platform multimedia frameworks unless they are clearly necessary

The first release should solve the core problem without turning the host package into a large dependency bundle.

## GPU-offload readiness

The first implementation does not need to require GPU acceleration, but the architecture should make later offload practical.

### Design rules for GPU readiness

- keep audio/video processing stages modular and separately measurable
- avoid tightly coupling speaker-focus logic to CPU-only image manipulation helpers
- define clean internal representations for per-frame metadata, crop windows, and beamforming state
- prefer algorithms that can later be mapped to OpenGL, OpenCL, Vulkan compute, or shader-style image passes
- keep depth-to-color alignment and crop/scale stages isolated so they can be moved to GPU-backed paths later

### Best early candidates for future GPU offload

- speaker-focus crop, scale, and compositing passes
- depth-guided region masking
- portions of frequency-domain beamforming or post-filtering if CPU usage becomes significant

## Testing and Validation Plan

## Offline DSP validation

- Build a library of 4-channel WAV fixtures captured from known positions.
- Verify beamformer output quality against raw mono mixes.
- Measure angle estimation error from controlled recordings.
- Track latency and CPU usage on realistic host hardware.

## Integration validation

- Confirm the audio service starts cleanly on Bazzite host installs.
- Confirm the processed microphone appears predictably in PipeWire-aware apps.
- Confirm `Kinect_Color` and `Kinect_SpeakerFocus` can coexist.
- Confirm speaker focus degrades gracefully when depth is unavailable or confidence is low.

## User-visible validation

- Single-speaker desk test
- Two-person couch test
- Multi-person boardroom test
- Background-noise rejection test
- Rapid speaker handoff test
- Auto-framing stability test

## Risks and Mitigations

### Risk: quiet or inconsistent raw capture

Mitigation:

- calibrate per-channel gain early
- add AGC as part of the MVP audio chain
- preserve raw recordings for regression analysis

### Risk: unstable speaker switching

Mitigation:

- require VAD plus confidence thresholds
- add hold time and hysteresis for target changes
- fall back to wide framing when confidence collapses

### Risk: poor target association in multi-person scenes

Mitigation:

- treat audio azimuth as a coarse cue
- use depth clustering to choose likely foreground speakers
- defer vision-heavy person identification until after the MVP works

### Risk: Bazzite packaging complexity

Mitigation:

- keep services host-side
- keep Flatpak as a thin controller
- reuse the current systemd/config/control patterns already present in the repository

### Risk: CPU load becomes too high

Mitigation:

- profile every major stage early
- keep the pipeline modular so expensive stages can move to GPU-backed implementations later
- prefer low-cost MVP algorithms first, then selectively optimize hot paths

## Recommended Delivery Order

1. Calibrate and document the raw mic array behavior on Linux
2. Add `openkinect-audiod` with raw capture plus focused mono output
3. Implement manual steering plus delay-and-sum beamforming
4. Add GCC-PHAT localization and metadata output
5. Add `Kinect_SpeakerFocus` as a second camera output
6. Fuse audio azimuth with depth-based target selection and auto-framing
7. Add stereo mode and expanded control-surface support
8. Add advanced DSP and targeted GPU offload where profiling justifies it

## Clarified implementation decisions

The following product decisions are now clarified:

1. the first shipping target is **Bazzite/PipeWire only**
2. the implementation should use **as few new dependencies as possible**
3. **color + internal depth access** is acceptable and preferred for speaker focus
4. the architecture should preserve a path for **raw, focused mono, and stereo** output modes with future Python GUI selection
5. speaker focus should behave like **active-speaker auto-framing**
6. the design should remain friendly to **future GPU/shader offload**

## Recommended first implementation milestone

The best first milestone is:

- native audio daemon
- raw 4-channel capture preserved
- processed focused mono microphone published through PipeWire
- GCC-PHAT azimuth estimate with status output
- no speaker-focus camera output yet

That milestone creates the foundation for both directional audio and speaker-focus video without forcing the project to solve every hard synchronization and target-selection problem at once.
