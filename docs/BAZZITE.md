# Bazzite Installation

OpenKinect v2 uses a split deployment model on Bazzite:

- **Host companion package** for the native daemon, systemd service, libfreenect2 access, and `v4l2loopback`
- **Flatpak launcher** for start/stop/status control through `flatpak-spawn --host`

## Host package workflow

1. Build or install the host RPM.
2. Layer it persistently with `rpm-ostree install ./openkinect-v2-*.rpm`.
3. Reboot if `rpm-ostree` requests it.
4. Edit `/etc/openkinect-v2/openkinect-v2.conf` to enable RGB, IR, or depth.
5. Enable the service:

```bash
sudo systemctl enable --now openkinect-v2.service
```

## Required host dependencies

The host must provide:

- `libfreenect2`
- `v4l2loopback`
- `PipeWire` or PulseAudio utilities for audio helpers

On Fedora/Bazzite, `akmod-v4l2loopback` is the expected module package.

## Stream labels

When enabled, the host runner provisions labeled loopback devices:

- `Kinect_Color`
- `Kinect_IR`
- `Kinect_Depth`

Applications can discover the stream they need by label instead of relying on fixed `/dev/videoN` paths.

## Audio helpers

The host package installs microphone helper scripts in `/usr/libexec/openkinect-v2/`.

Useful commands:

```bash
/usr/libexec/openkinect-v2/openkinect-audio-status.sh
/usr/libexec/openkinect-v2/kinect-audio-setup.sh
/usr/libexec/openkinect-v2/kinect-record.sh 5 sample.wav
```

## Flatpak launcher

The first Flatpak release is intentionally minimal. It forwards control actions to the host companion package:

```bash
flatpak run org.openkinect.OpenKinectV2 status
flatpak run org.openkinect.OpenKinectV2 start
flatpak run org.openkinect.OpenKinectV2 stop
flatpak run org.openkinect.OpenKinectV2 audio-status
```
