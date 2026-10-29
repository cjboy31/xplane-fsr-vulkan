<img src="assets/app-icon.png" width="96" alt="Original aircraft and temporal-pixel icon">

# FSR 3/4 + Frame Gen for X-Plane 12

An experimental Windows Vulkan integration for AMD FSR Native AA, upscaling and
frame generation, with a new current-frame cockpit-screen compositor for the
default Cirrus SR22. Original project code and artwork are MIT licensed.

The application is **FSR 3/4 + Frame Gen for X-Plane**. **FSR Ghosting Fix** is
the optional cockpit-screen feature, not the application name.

**Developer preview.** This project is being opened for community development.
GPU fixtures pass on a Radeon AI PRO R9700; the new SR22 screen projection and
visual quality still need live-flight validation. It is not an official AMD or
Laminar Research product, and is not a complete solution to ghosting or stutter.

## Features

- Modern charcoal/champagne native launcher with rounded panels, clear focus
  states, resizing/scrolling, accessible field names and persistent launch/status
  controls. Presets and MSAA/FXAA remain on the main screen.
- **Platform support:** current packages are Windows x64 only. Linux requires
  a native port; macOS requires a separate Metal implementation. See the
  [platform audit and port requirements](docs/PLATFORMS.md).
- Fresh logs for every simulator process, stale camera-frame rejection and
  temporary native rendering/FG pause for a grounded external camera near
  terrain in 0.5.9. See [ground-camera behavior and testing](docs/RENDERING-STABILITY.md).
- Frame-generation source matching/synchronization and safer temporal history
  handling in 0.5.6. See [stability changes and retest steps](docs/RENDERING-STABILITY.md).
- Automatic startup checks for GitHub Releases, preview-channel selection and
  verified one-click installation from **Updates...**, preserving saved choices.
  Install 0.5.4 or later once to enable this feature.
- Explicit Vulkan layer enabled only for X-Plane started by the launcher.
- FSR 3.1.6 compatibility and 4.1.2 ML-preview AA/upscaling providers.
- Optional FSR frame generation with SDK-controlled presentation.
- Fourteen presets on the main screen: seven resolution/AA choices each for
  FSR 3 and FSR 4, with backups and selective restoration.
- Automatically saved presets, provider, frame generation, checkboxes, simulator
  folder and arguments; custom AA/output choices and pending edits are remembered without changing FSR/FG.
- Hover/click help beside settings and a live injection banner. Green FSR ACTIVE
  requires recent renderer activity; a one-time Windows notification is requested.
- **Sim setup...** provides the required AA/input/output configuration and a
  read-only saved-settings check. Start with the [setup checklist](docs/INSTALLATION.md).
- **FSR Ghosting Fix - cockpit screens**, an independent Vulkan compositor:
  replays current-scene screen pixels
  after temporal reconstruction, with jitter correction, depth checks, edge
  erosion and an alignment tint. Initial target: default Cirrus SR22 PFD/MFD.
- Camera-pose stability checks and a separate IPC ABI for the new publisher.
- Main-screen MSAA, FXAA, input scale and output controls, with experimental 2x/4x/8x MSAA depth capture for FSR 3/4. FXAA remains native-only.
- Optimized camera/cockpit motion packing with measured GPU timings and numerical scene-flow tests; see [research and limits](docs/MOTION-VECTORS.md).
- CPU source-pacing diagnostics and isolated AA/FG/cockpit GPU tests.

The cockpit-screen fix currently requires **frame generation Off** and supports
only the **default Cirrus SR22 PFD/MFD**. The generic feature name does not extend
aircraft compatibility. It restores
pixels from the current rendered scene; it does not extract the native-resolution
G1000 texture. Upscaled screen detail therefore remains limited by input
resolution. Clouds, props, other aircraft and generated-frame UI are separate
problems. See [status and limitations](docs/STATUS.md).

## Build and run

Read [BUILDING.md](BUILDING.md). You need Windows x64, LLVM-MinGW, Python 3,
glslang, Vulkan-Headers, the X-Plane Plugin SDK and AMD FidelityFX SDK 2.4.0.
Third-party SDKs and runtime binaries are downloaded separately under their
respective terms. They are not committed here.

Build output goes to `app/`; run `XPlaneFSR24.exe` from that folder. Do not copy
an individual executable away from its layer, plugin and runtime directories.
Read [installation and testing](docs/INSTALLATION.md) before using it in X-Plane.

## Help develop it

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and [the roadmap](docs/ROADMAP.md).
Useful contributions include verifying SR22 projection/attachment alignment,
capturing frame-time regressions, native display-texture identification, better
per-object motion, FG UI composition and support for more aircraft.

Please distinguish synthetic-fixture results from live simulator results. Bug
reports should include simulator build, GPU/driver, aircraft, render/output
resolution and which AA/FG/cockpit options were enabled. Review logs before
posting them; do not upload paid aircraft assets or proprietary shaders.

## Licensing and provenance

The original launcher, layer, camera publisher, compositor, tests and icon are
MIT licensed in [LICENSE](LICENSE). Third-party notices are in [licenses/](licenses/).
AMD's signed runtime DLLs have separate binary redistribution terms; they are
not open-sourced by this repository. X-Plane is separately owned software.

The [MSFS glass-cockpit fix's public explanation](https://github.com/IFreemz/msfs2024-glass-cockpit-dlss-fix#how-the-fix-works)
informed the research. No source, shaders or binaries from that mod are included.
The new compositor is an independent implementation. Aircraft display geometry
is read from the user's installed SR22 at runtime and is never embedded here.

No endorsement by AMD, Laminar Research, X-Plane or reference-mod authors is claimed.
