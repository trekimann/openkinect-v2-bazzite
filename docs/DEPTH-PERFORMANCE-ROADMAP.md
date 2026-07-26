# Kinect v2 Depth Performance Roadmap

## Current state

The current Bazzite host path intentionally builds `libfreenect2` in a minimal CPU-only configuration:

```bash
-DENABLE_OPENCL=OFF
-DENABLE_CUDA=OFF
-DENABLE_OPENGL=OFF
```

That choice made the first install path reliable on Bazzite, but it also means the depth path is using the CPU packet processor.

From the live service logs on the Bazzite host:

- `TurboJpegRgbPacketProcessor` is usually around `10-13ms` per cycle
- `CpuDepthPacketProcessor` is usually around `90-100ms` per cycle
- `DepthPacketStreamParser` repeatedly reports lost packets

That tells us the primary depth bottleneck is not the false-color palette code in `openkinect-v2d`. The dominant cost is depth packet decoding and processing inside `libfreenect2`.

## What is fast vs slow today

### Cheap in our daemon

These parts are not the main problem:

- depth palette blending in [camera/kinect2v4l2.cpp](camera/kinect2v4l2.cpp)
- config reload on `SIGHUP`
- YUYV write-out to the loopback device

The new three-stop palette is a tiny per-pixel math step compared with depth packet reconstruction.

### Expensive today

The heavy path is:

1. Kinect raw depth packet decode in `libfreenect2`
2. CPU depth reconstruction via `CpuDepthPacketProcessor`
3. Only after that does our daemon colorize and write the frame

## Existing hooks already in the codebase

The project already has pipeline selection hooks in [camera/kinect2v4l2.cpp](camera/kinect2v4l2.cpp):

- `PIPELINE=auto`
- `PIPELINE=opencl`
- `PIPELINE=opengl`
- CPU fallback if GPU packet pipelines are not available

The selection logic is in `make_pipeline()`, but the current Bazzite installer builds `libfreenect2` without OpenCL or OpenGL support, so `auto` effectively resolves to CPU today.

## Recommended acceleration order

### Phase 1: Enable libfreenect2 GPU packet pipelines

This is the highest-value move.

Goal:

- keep the daemon mostly unchanged
- move depth packet processing out of `CpuDepthPacketProcessor`
- use `OpenCLPacketPipeline` first, `OpenGLPacketPipeline` second

Why this is the best first step:

- the current logs already identify CPU depth packet processing as the bottleneck
- the code already knows how to select GPU pipelines
- this targets the expensive stage, not just the final color mapping stage

Implementation work:

1. Add an optional GPU-enabled `libfreenect2` build mode to [scripts/install-bazzite-host.sh](scripts/install-bazzite-host.sh)
2. Install the required Bazzite host dependencies for OpenCL or OpenGL development
3. Build `libfreenect2` with `-DENABLE_OPENCL=ON` and optionally `-DENABLE_OPENGL=ON`
4. Expose pipeline choice clearly in `/etc/openkinect-v2/openkinect-v2.conf`
5. Measure packet loss and per-frame processing time again

Success criteria:

- depth processor log drops well below the current `90-100ms`
- packet loss decreases materially
- `PIPELINE=auto` selects OpenCL or OpenGL on supported hosts

### Phase 2: Separate throughput from presentation

Once packet processing is accelerated, we should check whether presentation is still wasting cycles.

Areas to inspect:

- extra BGRA allocation in `colorize_depth_frame()`
- BGRA -> YUYV conversion cost
- copying into loopback buffers every frame

Potential improvements:

1. reuse the depth color buffer instead of reallocating every frame
2. avoid duplicate conversions where possible
3. benchmark whether writing depth at a lower target FPS while keeping color at full FPS gives a better overall UX on smaller hardware

### Phase 3: Optional shader-based depth colorization

Shader acceleration is still worth considering, but it should come after Phase 1.

Why it is not first:

- our palette code is not the hot path today
- accelerating only the false-color step will not fix depth decode stalls if packet reconstruction remains CPU-bound

Where shader work could help later:

1. map normalized depth to RGB on GPU instead of CPU
2. support richer palettes, gradients, and dynamic range visualization at low CPU cost
3. prepare for future GUI-driven effects and visual presets

Recommended shape:

- keep the raw depth normalization boundary explicit
- allow a later GLSL/OpenCL kernel path for colorization
- retain the CPU palette path as fallback

## Bazzite-specific constraints

### Constraint 1: current installer optimizes for reliability

The Bazzite installer currently prefers a CPU-only `libfreenect2` build because it avoids a wider dependency surface and made the first successful host install much easier.

### Constraint 2: immutable host model

Any GPU acceleration path has to be compatible with the Bazzite host deployment model:

- host packages may require additional `rpm-ostree` layering
- OpenCL/OpenGL build dependencies must be installed on the host, not just in the dev container
- the daemon still runs as a host systemd service

### Constraint 3: smaller target hardware

The final target machine is smaller than the current VM-backed development setup. That makes it even more important to fix the real bottleneck in depth packet processing first rather than polishing only the presentation layer.

## Suggested immediate next experiments

1. Add a guarded installer option such as `OPENKINECT_ENABLE_OPENCL=1` in [scripts/install-bazzite-host.sh](scripts/install-bazzite-host.sh)
2. Add host dependency discovery for OpenCL/OpenGL packages on Bazzite
3. Rebuild `libfreenect2` with GPU packet pipelines enabled
4. Test the same stream set with `PIPELINE=auto`, then `PIPELINE=opencl`, then `PIPELINE=opengl`
5. Compare:
   - packet loss frequency
   - depth processor average time
   - service stability
   - final OBS smoothness on `Kinect_Depth`

Status:

- The Bazzite installer now defaults to an OpenCL-enabled `libfreenect2` build on supported hosts.
- The OpenCL path has been validated on the Bazzite host with an NVIDIA RTX 5070.
- With `PIPELINE=opencl`, `OpenCLDepthPacketProcessor` is running at roughly `0.42-0.49ms` per cycle.
- The prior CPU path was roughly `90-110ms` per cycle in `CpuDepthPacketProcessor`.
- This confirms the primary depth bottleneck was packet processing in `libfreenect2`, and that moving that stage to OpenCL materially improves throughput.

## Summary

If the goal is fewer dropped depth packets and smoother depth video, the first serious acceleration target is `libfreenect2` depth packet processing, not the final false-color palette stage.

The right order is:

1. enable and validate OpenCL/OpenGL packet pipelines in `libfreenect2`
2. reduce extra allocations and conversions in our daemon if needed
3. only then invest in shader-based colorization for presentation quality and richer controls