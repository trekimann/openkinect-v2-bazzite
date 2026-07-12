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
- `PipeWire`

On Fedora/Bazzite, `akmod-v4l2loopback` is the expected module package once RPM Fusion is enabled on the host.

## Local developer build

For a first compile in a Fedora or Bazzite development shell, install the native build toolchain first:

```bash
sudo dnf install -y \
	cmake gcc-c++ make ninja-build pkgconf-pkg-config \
	libusb1-devel pipewire-devel turbojpeg-devel
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

To iterate on the audio service without a local `libfreenect2` install, you can build the PipeWire daemon by itself:

```bash
cmake -S . -B build-audio -G Ninja -DOPENKINECT_BUILD_CAMERA=OFF
cmake --build build-audio
```

If you run `./build/openkinect-v2d` directly from a containerized development shell, USB access may still fail with `LIBUSB_ERROR_ACCESS`. The expected Bazzite runtime path is the host companion install plus the systemd service, which owns `v4l2loopback` setup and runs with host-level device access.

## Stream labels

When enabled, the host runner provisions labeled loopback devices:

- `Kinect_Color`
- `Kinect_IR`
- `Kinect_Depth`

Applications can discover the stream they need by label instead of relying on fixed `/dev/videoN` paths.

## Audio service and helpers

The host package installs:

- `openkinect-audiod` as a **user** systemd service (`openkinect-audio.service`)
- microphone helper scripts in `/usr/libexec/openkinect-v2/`
- a runtime metadata file under `${XDG_RUNTIME_DIR}/openkinect-v2/audio-state.json` by default

Useful commands:

```bash
/usr/libexec/openkinect-v2/openkinect-audio-status.sh
/usr/libexec/openkinect-v2/kinect-audio-setup.sh
/usr/libexec/openkinect-v2/kinect-record.sh 5 sample.wav
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh audio-start
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh audio-status
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh audio-direction
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh audio-mode focused-mono
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh restart
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode color
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode ir
/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh mode depth
```

Enable the audio service for the current desktop user:

```bash
systemctl --user enable --now openkinect-audio.service
```

## Flatpak launcher

The first Flatpak release is intentionally minimal. It forwards control actions to the host companion package:

```bash
flatpak run org.openkinect.OpenKinectV2 status
flatpak run org.openkinect.OpenKinectV2 start
flatpak run org.openkinect.OpenKinectV2 stop
flatpak run org.openkinect.OpenKinectV2 audio-start
flatpak run org.openkinect.OpenKinectV2 audio-status
flatpak run org.openkinect.OpenKinectV2 audio-direction
flatpak run org.openkinect.OpenKinectV2 audio-mode focused-mono
flatpak run org.openkinect.OpenKinectV2 mode color
flatpak run org.openkinect.OpenKinectV2 mode ir
flatpak run org.openkinect.OpenKinectV2 mode depth
```
