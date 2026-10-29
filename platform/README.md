# Native platform development preview

This branch adds a **Linux x64 FSR 3.1 Vulkan preview** and a **universal Mac
diagnostic plugin**, with a shared Python/Tk launcher. It does not claim full
Linux/Mac parity with Windows. Read [installation and limits](INSTALLATION-AND-USE.txt).

The Linux renderer is built from JonathanOrr's MIT
[xplane-fsr3](https://github.com/JonathanOrr/xplane-fsr3) at the pinned commit
`fa51577c9bc2c3477e64dfdd358796aebb9ca45b`. Its FSR 3.1.5 backend is built from
AMD's MIT SDK v1.1.4 plus the upstream Linux patches. That older open backend
provides a native Linux build; it is not a port of the Windows FSR 4 provider.
Dependency notices are copied into the packages. No third-party binary download
is repackaged as an original renderer.

The Linux path uses camera/depth from recognized Vulkan scene passes, requires
FSR supersampling enabled and MSAA off, and falls back if inputs are unavailable.
No complete moving-object/cloud motion, reactive masks, Windows cockpit
compositor, ground-camera safeguard, Native AA, VR, FSR 4 or FG is provided.
Community flight tests are required before recommending it as a stable release.

The Mac `mac.xpl` is universal arm64/x86_64. It checks native plugin loading,
SDK/simulator version and finite camera matrices, writes an atomic report every
two seconds, and exposes an explicitly named diagnostic menu. It never takes
camera control, reads joystick input, writes datarefs or draws/modifies a frame.
macOS X-Plane renders Metal; no Metal scene/depth/presentation bridge is
implemented. Native plugin loading is useful groundwork, not Mac FSR support.
Ad-hoc signing is not notarization.

## Build and validate

On Ubuntu 22.04 x64, install `cmake g++-12 libvulkan-dev glslang-tools
mesa-vulkan-drivers vulkan-validationlayers python3-tk`. On macOS, install Xcode
command-line tools, CMake and Python 3.10+. Then:

```sh
python3 -m unittest discover -s platform/tests -p 'test_core.py' -v
python3 platform/build_native.py
```

The build downloads the SHA-256-pinned official SDK 4.4.0-r1 for declarations;
no Laminar runtime is packaged. Linux builds also fetch the commit-pinned MIT
renderer/AMD source. Outputs are under ignored `dist/native`; dependencies and
build intermediates are under ignored `work/native`.
Set `CC=gcc-12 CXX=g++-12` for the Linux build; the fixture uses `_Float16`,
which GCC 11 does not provide on x64. Current Vulkan headers are fetched at a
pinned commit so older distribution headers do not limit compilation. The
packaged layer is still checked against the stated glibc/libstdc++ floors.

The hosted workflow builds on Ubuntu 22.04 and macOS 14. It checks persisted
choices, isolated launch environments, fresh-session status, probe ownership,
actual native plugin load/unload through a simulated host, both Mac architecture
slices, Linux ELF dependency floors, Vulkan reconstruction against a synthetic
ground truth under Lavapipe/validation, payload hashes and an extracted archive
install/update/uninstall round trip. It does **not** run X-Plane, certify a GPU,
exercise Metal rendering, or verify visual quality in a flight. Intel Mac slices
are built but not executed by the Apple Silicon runner.

## Release policy for these native previews

Publish on the separate `experimental/native-platforms` branch using a
`native-preview-*` tag and GitHub pre-release. Keep the Windows release/version
unchanged. The packages are native `.tar.gz`/`.zip` archives with a combined RAR,
instructions and hashes; Windows setup aliases would be misleading and must not
be supplied. Existing Windows automatic update matching must not select these
previews. There is no native automatic updater yet.

## Mac rendering work still required

Obtain a supported way to access X-Plane's Metal scene color, depth, motion and
presentation boundaries, or design an explicitly different capture-based path.
Select/review a redistributable Metal reconstruction backend. Handle frame
ownership, UI composition, synchronization, resolution changes and signing.
Validate on actual Apple Silicon and Intel hardware before claiming rendering
support. The SDK 4.4 Panel Graphics/ImGui APIs are for plugin UI and do not supply
this bridge; see [Laminar's release](https://github.com/X-Plane/xpsdk/releases/tag/xpsdk_4.4.0-r1)
and [Panel Graphics reference](https://xpsdk-api.docs.staging.x-plane.com/).

NR development stays canceled; built-in FG remains off in regular releases.
