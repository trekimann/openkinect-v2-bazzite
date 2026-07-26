# Bazzite Installation

OpenKinect v2 uses a split deployment model on Bazzite:

- **Host companion package** for the native daemon, systemd service, libfreenect2 access, and `v4l2loopback`
- **Flatpak launcher** for start/stop/status control through `flatpak-spawn --host`

## Host package workflow

For the current first-pass workflow, run the repo installer directly:

```bash
./install.sh
```

When launched from a distrobox/dev container, the installer re-executes itself on the Bazzite host with `distrobox-host-exec`.

1. If RPM Fusion is not already configured on the host, the first run layers the RPM Fusion release packages with `rpm-ostree` and exits.
2. Reboot if `rpm-ostree` stages a new deployment.
3. Rerun `./install.sh` after reboot.
4. The next run layers missing host packages, including `akmod-v4l2loopback`, the CMake toolchain, and the `libfreenect2` build dependencies.
5. Reboot again if `rpm-ostree` stages another new deployment.
6. Rerun `./install.sh` after reboot.
7. The final run builds and installs `libfreenect2`, installs the OpenKinect host service, and starts `openkinect-v2.service`.
8. Edit `/etc/openkinect-v2/openkinect-v2.conf` to enable RGB, IR, or depth.

The service can also be enabled manually if needed:

```bash
sudo systemctl enable --now openkinect-v2.service
```

## Required host dependencies

The host must provide:

- `libfreenect2`
- `v4l2loopback`
- `PipeWire` or PulseAudio utilities for audio helpers

On Fedora/Bazzite, `akmod-v4l2loopback` is the expected module package once RPM Fusion is enabled on the host.

## Local developer build

For a first compile in a Fedora or Bazzite development shell, install the native build toolchain first:

```bash
sudo dnf install -y \
	cmake gcc-c++ make ninja-build pkgconf-pkg-config \
	libusb1-devel turbojpeg-devel
```

`libfreenect2` is not assumed to be available as a packaged development dependency, so build a minimal local copy into `/usr/local`:

```bash
git clone --depth 1 https://github.com/OpenKinect/libfreenect2.git /tmp/libfreenect2
cmake -S /tmp/libfreenect2 -B /tmp/libfreenect2/build -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr/local \
	-DBUILD_EXAMPLES=OFF \
	-DBUILD_OPENNI2_DRIVER=OFF \
	-DENABLE_CXX11=ON \
	-DENABLE_OPENCL=OFF \
	-DENABLE_CUDA=OFF \
	-DENABLE_OPENGL=OFF \
	-DENABLE_VAAPI=OFF \
	-DENABLE_TEGRAJPEG=OFF
cmake --build /tmp/libfreenect2/build
sudo cmake --install /tmp/libfreenect2/build
sudo ldconfig
```

Once `libfreenect2` is installed, the project should configure and build normally:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

If you run `./build/openkinect-v2d` directly from a containerized development shell, USB access may still fail with `LIBUSB_ERROR_ACCESS`. The expected Bazzite runtime path is the host companion install plus the systemd service, which owns `v4l2loopback` setup and runs with host-level device access.

For the next round of depth performance work, see [docs/DEPTH-PERFORMANCE-ROADMAP.md](docs/DEPTH-PERFORMANCE-ROADMAP.md). The current host bootstrap intentionally uses a CPU-only `libfreenect2` build for reliability, and that is now the main depth performance constraint.

## Experimental OpenCL path

The host has an experimental OpenCL-enabled install path for depth acceleration:

```bash
OPENKINECT_ENABLE_OPENCL=1 ./install.sh
```

After that build completes, set the host config to:

```ini
PIPELINE=opencl
```

Then restart the service:

```bash
sudo systemctl restart openkinect-v2.service
```

The current recommendation is to treat this as an optimization path to compare against the default CPU build, not the default install mode yet.

Current measured result on the Bazzite development host with an NVIDIA RTX 5070:

- CPU depth path: roughly `90-110ms` in `CpuDepthPacketProcessor`
- OpenCL depth path: roughly `0.42-0.49ms` in `OpenCLDepthPacketProcessor`

That is a large enough improvement that `PIPELINE=opencl` should be considered the preferred depth-processing mode on supported hosts.

## Stream labels

When enabled, the host runner provisions labeled loopback devices:

- `Kinect_Color`
- `Kinect_IR`
- `Kinect_Depth`

Applications can discover the stream they need by label instead of relying on fixed `/dev/videoN` paths.

## Live depth palette controls

The host config now supports a three-stop blended depth palette:

```ini
DEPTH_NEAR_COLOR=#0000FF
DEPTH_MID_COLOR=#00FF00
DEPTH_FAR_COLOR=#FF0000
```

These colors blend near -> middle -> far in the running depth renderer.

To update the palette live on the host without restarting the service:

```bash
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh depth-colors '#AA0000' '#00AA00' '#0000AA'
```

From a dev container on the same machine, the host-safe form is:

```bash
distrobox-host-exec bash -lc "sudo /usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh depth-colors '#AA0000' '#00AA00' '#0000AA'"
```

Stream mode changes still restart the service:

```bash
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode color
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode ir
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode depth
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode all
```

## Audio helpers

The host package installs microphone helper scripts in `/usr/libexec/openkinect-v2/`.

Useful commands:

```bash
/usr/libexec/openkinect-v2/openkinect-audio-status.sh
/usr/libexec/openkinect-v2/kinect-audio-setup.sh
/usr/libexec/openkinect-v2/kinect-record.sh 5 sample.wav
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh restart
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode color
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode ir
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode depth
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh depth-colors '#AA0000' '#00AA00' '#0000AA'
```

## Flatpak launcher

The control app now opens a small local control panel by default. It reads host state through the existing host bridge and exposes stream mode plus live depth palette controls.

Launch the control panel:

```bash
flatpak run org.openkinect.OpenKinectV2
```

By default it serves the UI on `http://127.0.0.1:40123/`, opens that URL in your browser automatically, and falls back to another local port only if `40123` is already in use.

You can also launch the same control panel directly from a dev container or local shell with:

```bash
python3 packaging/flatpak/openkinect-control.py
```

CLI forwarding still works for direct control commands:

```bash
flatpak run org.openkinect.OpenKinectV2 status
flatpak run org.openkinect.OpenKinectV2 start
flatpak run org.openkinect.OpenKinectV2 stop
flatpak run org.openkinect.OpenKinectV2 audio-status
flatpak run org.openkinect.OpenKinectV2 mode color
flatpak run org.openkinect.OpenKinectV2 mode ir
flatpak run org.openkinect.OpenKinectV2 mode depth
flatpak run org.openkinect.OpenKinectV2 depth-colors '#AA0000' '#00AA00' '#0000AA'
```
