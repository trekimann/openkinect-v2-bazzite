# Kinect v2 Heart Rate Monitoring Roadmap

## Objective

Add an optional remote photoplethysmography (rPPG) feature that estimates heart rate from Kinect v2 facial video and publishes:

1. a new augmented video loopback feed with live BPM overlays
2. a lightweight local UDP data stream for external consumers

The feature must preserve the current baseline behavior when disabled, keep dependencies minimal for the first pass, and fit the existing Bazzite host service model.

## Confirmed product decisions

- labeled devices are acceptable; fixed `/dev/video50` and `/dev/video51` numbering is not required
- the heart-rate camera is a new video feed rather than an overlay on the existing clean color feed
- local UDP is preferred over FIFO for the raw numeric stream
- visual output is the priority; UDP output can stay secondary in the first implementation
- first pass should prefer minimal new dependencies even if face tracking quality is lower
- a reusable OpenGL compute example now exists for the depth stream and should be used as the shader reference when wiring GPU ROI reduction

## Current codebase fit

### Existing camera daemon

The main camera service is currently built from `camera/kinect2v4l2.cpp` and already provides:

- config-file driven stream enablement
- labeled V4L2 loopback discovery
- concurrent RGB, IR, and depth output
- packet pipeline selection through `PIPELINE=auto|opencl|opengl`

That makes it the correct insertion point for rPPG.

### Existing config and service shape

The current service stack already matches the intended deployment model:

- `services/openkinect-v2.conf` holds runtime configuration
- `camera/openkinect-v2-runner.sh.in` provisions loopback labels and launches the daemon
- `scripts/openkinect-v2-hostctl.sh.in` exposes host-side control entrypoints
- `packaging/flatpak/openkinect-control.py` is a thin launcher that forwards commands to the host

The heart-rate feature should extend this model rather than introduce a separate daemon.

### GPU context

The repository already supports OpenCL/OpenGL packet pipeline selection through `libfreenect2`, and `docs/DEPTH-PERFORMANCE-ROADMAP.md` documents the existing acceleration direction. The new heart-rate work should reuse the depth-stream OpenGL compute example as the starting point for GPU-side ROI aggregation instead of inventing a second graphics path from scratch.

## Proposed first-pass scope

### In scope

- optional rPPG feature flag and config surface
- new augmented heart-rate loopback feed
- CPU-side lightweight face detection/tracking with minimal dependencies
- GPU-assisted ROI reduction for spatial averaging
- per-face rolling signal buffers
- BPM estimation for `rgb` source first
- UDP loopback stream for raw heart-rate values
- confidence scoring and smoothing

### Deferred or explicitly secondary

- production-quality multi-landmark mesh tracking
- richer IPC protocols beyond simple UDP datagrams
- advanced IR-only estimator parity
- UI/GUI controls beyond config plus host control scripts
- replacing the clean RGB feed with overlays

## Recommended architecture

### 1. Keep the clean color feed intact

Do not mutate the existing `Kinect_Color` output. Add a separate labeled loopback feed such as `Kinect_HeartRate` so existing apps can continue to use the clean camera without behavior changes.

### 2. Add an rPPG feature branch inside the main frame loop

The daemon should keep its current capture loop but branch the color path when rPPG is enabled:

1. capture color frame
2. publish clean color output as today
3. run face detection/tracking on a downscaled CPU working copy
4. send active facial ROIs to the GPU reduction stage
5. update per-user rolling signal buffers
6. estimate BPM and confidence
7. render overlays onto a second output frame
8. write the augmented frame to the new loopback device
9. emit UDP datagrams if the current estimate passes the minimum confidence threshold

### 3. Separate person state from frame state

Introduce explicit per-person tracking state so each detected face maintains:

- stable `user_id`
- current face rectangle
- forehead and cheek ROIs
- last-seen timestamp
- rolling signal buffer
- filtered buffer
- current BPM
- smoothed BPM
- confidence

This avoids re-deriving history on every frame and makes the UDP stream straightforward.

## Stream design

### Stream A: augmented video loopback

Add a fourth optional video output dedicated to rPPG presentation:

- label: `Kinect_HeartRate` by default
- source: color camera
- content: clean RGB plus face indicator and BPM overlay
- lifecycle: only created when rPPG is enabled

Keep label-based discovery consistent with the rest of the repo. Only add fixed device-number support if a downstream tool later proves that labels are insufficient.

### Stream B: raw UDP loopback data

Use UDP loopback on `127.0.0.1:4243` for the first pass.

Suggested payload fields:

- `user_id`
- `source`
- `bpm`
- `confidence`
- `timestamp_ms`
- `tracking_state`

Recommended first-pass format:

- newline-delimited JSON or another self-describing text format
- single datagram per user update
- best-effort send only; never block the video path for UDP delivery

Because video is higher priority than data capture, UDP should run behind a hard failure boundary: if socket setup or send fails, log it and continue serving the camera feed.

## Signal source plan

### Phase 1: RGB rPPG first

Start with `--rppg-source=rgb` and make it the most complete path.

Rationale:

- the cleanest user-facing requirement is a visible BPM overlay on the augmented color feed
- the RGB path aligns naturally with face detection and overlay rendering
- the first-pass algorithm can work with simple ROI averaging and a band-limited frequency estimator

### Phase 2: IR mode behind the same interface

Expose `--rppg-source=ir` in the config and CLI surface, but treat it as follow-on implementation work. The IR mode can reuse tracked face geometry from the color path and then project those ROIs into the IR frame space.

## Face detection and masking strategy

### Minimal-dependency first pass

Use a lightweight CPU-side detector rather than adding a large mesh framework immediately.

Preferred first-pass approach:

1. detect faces from a downscaled color frame
2. derive a normalized face rectangle
3. synthesize forehead and upper-cheek ROIs from the rectangle geometry
4. reject faces that are too small, clipped, or too unstable

This keeps the implementation simple and matches the project preference for minimal new dependencies. Tracking quality can be revisited later with A/B testing if the first pass proves too noisy.

### ROI policy

Restrict sampling to skin-heavy zones:

- upper forehead
- left upper cheek
- right upper cheek

Avoid:

- mouth and jaw regions with frequent motion
- hairline if the box is unstable
- background pixels outside the inferred face region

## GPU offload plan

### Why GPU work is still needed

The feature must avoid turning per-frame ROI sampling into a large CPU cost. Even with lightweight face detection, spatial reduction across multiple facial regions should move to the GPU when available.

### What the compute path should do

Reuse the depth-stream OpenGL compute example to build an rPPG ROI reducer that:

1. uploads the current frame as a texture or image
2. uploads per-face ROI bounds and masks
3. computes spatial averages for the green channel or chrominance terms
4. optionally derives quick quality metrics such as variance or saturation clipping
5. reads back only a tiny aggregate buffer to the CPU

This keeps GPU readback small and avoids copying full intermediate images back to the CPU.

### First-pass fallback

Retain a CPU fallback for environments where the OpenGL compute path is unavailable or disabled. The CPU fallback should be functional but explicitly lower priority for performance tuning than the GPU path.

### Important constraint

The Bazzite host bootstrap and any local build instructions must account for the graphics dependencies needed by the depth compute example and the new ROI reducer. The current service model must still work when GPU acceleration is absent.

## Signal-processing plan

### Buffering

Maintain a rolling per-person buffer sized by time rather than assuming a perfectly fixed frame cadence.

Initial target:

- up to 256 recent samples
- expected around 8 to 9 seconds at 30 FPS

Store timestamps with samples so frequency estimation can tolerate small frame pacing jitter.

### Normalization

Each update cycle should:

1. subtract the DC baseline
2. remove slow illumination drift
3. normalize amplitude
4. reject obviously corrupted samples caused by sudden motion or lost tracking

### Frequency estimation

Restrict candidate heart-rate energy to the human range:

- 40 BPM to 180 BPM
- approximately 0.66 Hz to 3.0 Hz

For the first pass, either of these is acceptable:

- FFT peak picking inside the bounded range
- bandpass filtering plus dominant peak estimation

The algorithm should surface:

- instantaneous BPM candidate
- signal quality metric
- smoothed BPM for overlay display

### Smoothing and confidence

The overlay should display the smoothed BPM, not the raw per-frame estimate. Confidence should incorporate:

- face stability across frames
- ROI size and coverage
- spectral peak sharpness
- motion rejection results
- signal strength relative to noise floor

## Overlay design

The augmented video feed should render only when a face is confidently tracked.

Recommended first-pass overlay elements:

- simple bounding box or head anchor
- numeric BPM label
- optional confidence indicator

Do not over-design the UI in the first pass. The priority is a readable signal that works well in OBS or webcam consumers.

## Config and CLI additions

Add the following new config keys to `services/openkinect-v2.conf`:

- `ENABLE_RPPG=0|1`
- `RPPG_SOURCE=rgb|ir`
- `RPPG_LABEL=Kinect_HeartRate`
- `RPPG_DEVICE=`
- `RPPG_UDP_HOST=127.0.0.1`
- `RPPG_UDP_PORT=4243`
- `RPPG_OVERLAY=1`
- `RPPG_MIN_CONFIDENCE=...`
- `RPPG_BUFFER_SIZE=256`
- `RPPG_BPM_MIN=40`
- `RPPG_BPM_MAX=180`
- `RPPG_ENABLE_GPU=1`

Add matching daemon CLI overrides:

- `--enable-rppg`
- `--rppg-source=rgb|ir`

CLI should override config values so local testing can happen without editing the host config each time.

## Likely file touchpoints for implementation

- `camera/kinect2v4l2.cpp`
- `camera/openkinect-v2-runner.sh.in`
- `services/openkinect-v2.conf`
- `scripts/openkinect-v2-hostctl.sh.in`
- `scripts/install-bazzite-host.sh`
- `CMakeLists.txt`
- `README.md`
- `docs/BAZZITE.md`

If the depth-stream OpenGL compute example lives in a separate new source file or branch-local addition, wire that file into the same list before implementation starts.

## Suggested implementation phases

### Phase 1: plumbing and feature gating

1. extend config parsing and CLI argument parsing
2. add the new optional loopback output definition for the heart-rate feed
3. add UDP socket setup behind a feature flag
4. confirm that baseline RGB/IR/depth behavior is unchanged when rPPG is disabled

### Phase 2: lightweight detection and overlay scaffolding

1. add CPU-side face detection on downscaled color frames
2. derive simple forehead and cheek ROIs from each face box
3. add stable per-person IDs with basic temporal association
4. draw placeholder overlays on the new heart-rate feed before BPM is implemented

### Phase 3: GPU ROI reduction

1. adapt the existing depth OpenGL compute example to consume color-frame ROIs
2. compute aggregate ROI statistics on the GPU
3. add a CPU fallback reducer
4. benchmark the cost of the GPU and CPU paths

### Phase 4: BPM estimation

1. add rolling buffers and timestamps per tracked person
2. implement detrending and bounded frequency estimation
3. add smoothing and confidence scoring
4. publish BPM overlays and UDP data only when confidence is acceptable

### Phase 5: IR integration

1. project tracked face ROIs from color into IR frame coordinates
2. adapt the reducer for IR sampling
3. tune confidence and motion rejection separately from the RGB path

### Phase 6: packaging and documentation

1. update Bazzite host dependency handling if the compute example requires extra graphics packages
2. document the new config keys and runtime flags
3. add test instructions for OBS and local UDP consumers

## Validation checklist for local IDE work

### Functional checks

- clean `Kinect_Color` still works unchanged
- `Kinect_HeartRate` appears only when rPPG is enabled
- face overlay follows the subject on the heart-rate feed
- BPM only appears after enough samples are collected
- UDP datagrams are emitted without affecting camera stability
- multi-face scenarios keep stable IDs and separate BPM values

### Failure-mode checks

- no face in frame
- face partially out of frame
- fast movement
- lighting changes
- GPU path unavailable
- UDP port unavailable

### Performance checks

- color feed remains responsive at target frame rate
- heart-rate feed remains usable in OBS
- enabling UDP does not change visible frame pacing
- CPU fallback remains correct even if it is slower

### Bazzite-specific checks

- service starts from the host companion package
- labeled device discovery still works after loopback reload
- new config keys survive the expected host workflow
- Flatpak launcher remains a control surface rather than becoming a processing runtime

## Local handoff note

For the first implementation pass, optimize for:

1. feature-gated integration
2. a stable augmented visual feed
3. minimal new dependencies
4. reuse of the existing depth OpenGL compute example

Do not block first-pass development on perfect tracking quality or a rich UDP protocol. Get the augmented camera path stable first, then iterate on signal quality and external data consumers.
