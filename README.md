<img src="assets/app-icon.png" width="96" alt="Original aircraft and temporal-pixel icon">

# FSR 3/4 + Frame Gen for X-Plane 12

**Community:** [Join our Discord](https://discord.gg/yh396QNgHZ) · [Release updates](https://discord.com/channels/1565487749832777830/1565816821523681290)

Get setup help, report problems, share feedback and follow new releases.

An experimental Windows Vulkan integration for AMD FSR Native AA, upscaling and
frame generation, with a new current-frame cockpit-screen compositor for the
default Cirrus SR22. Original project code and artwork are MIT licensed.

The application is **FSR 3/4 + Frame Gen for X-Plane**. **FSR Ghosting Fix** is
the optional cockpit-screen feature, not the application name.

**Community preview.** GPU fixtures and selected X-Plane 12.4.4 live tests pass
on a Radeon AI PRO R9700. Broader aircraft, hardware, display and long-flight
quality still need validation. This is not an official AMD or Laminar Research
product, and is not a complete solution to ghosting or stutter.

## Downloads and current source

**Windows 0.7.2-alpha: in-sim controls preview and performance/memory fixes.**
Use the [Normal release](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.2-alpha).
In full setup, select your simulator and enable in-sim integration. Then launch
normally and open **Plugins > FSR 3/4 + Frame Gen**. The desktop application is
an **optional secondary interface**, retained for updates, guide playback and
full diagnostics. Both interfaces share saved choices. Existing automatic
updates refresh an enabled integration; desktop-only users run full setup once
to opt in. See [current instructions](docs/INSTALLATION-0.7.2.txt),
[panel limits](docs/IN-SIM-PLUGIN.md) and [measured CPU findings](docs/CPU-PERFORMANCE.md).
Third-party launcher combinations and full in-sim feature parity remain under test.

**Normal includes all current rendering features.** Both channels
include FSR 3/4, independent neural rendering, frame generation and optional
synchronized FG compatibility modes. Normal is recommended; Experimental is
for future development updates. NR and FG start Off. NR requires compatible
Windows RDNA4 FP8 hardware and your own supported local model. It supports
External only or Full scene scope and live Before/After upscaling order.
Before is the recommended lower-cost order. This is an independent integration,
not NVIDIA's official DLSS 5 implementation. No NVIDIA DLL or model weights are
included. See [NR setup](docs/NEURAL-RENDERING.md), the
[reference review](docs/FRAME-GENERATION-REFERENCE-REVIEW.md) and
[rendering evidence and limits](docs/RENDERING-STABILITY.md).

This update also includes live current-session logs, clearer layer activity,
initial optional-work deferral and safe real-frame holds when FG input is missing.
The corrected FG display encoding stopped flicker in the owner's R9700 live test;
broader hardware, display and long-flight quality remain unverified.
The [async FG investigation](docs/ASYNC-FRAME-GENERATION.md) includes controlled
results and developer test commands; it does not enable async in the launcher.

- **Windows Normal (recommended):** [0.7.2-alpha installer and instructions](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.2-alpha). All current NR, FG and compatibility features. NR + FG requires explicit opt-in and compatible hardware/model. Start with FG Off and one NR pass.
- **Windows Experimental:** [0.7.1-experimental.1 installer](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.1-experimental.1). Same current feature set; future development track. Switch builds through **Updates / Channel**.
- **Feature video:** [Watch or download the 0.7.1 guide](https://github.com/cjboy31/xplane-fsr-vulkan/releases/download/v0.7.1-alpha/XPlane-FSR3-4-0.7.1-alpha-Feature-Guide.mp4). Explains live NR order/controls, FG choices, startup behavior and logs, with captions and immediate Skip. [Versioned-video workflow](docs/FEATURE-VIDEOS.md).
- **Linux:** [Native FSR 3.1 preview downloads](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1) — requires MSAA off and FSR supersampling enabled; real-flight validation is pending.
- **Mac:** [Universal compatibility-probe download](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1) — diagnostics only; **Mac FSR/Metal rendering is not implemented**.
- **Combined Linux/Mac RAR:** [Download the native preview package](https://github.com/cjboy31/xplane-fsr-vulkan/releases/download/native-preview-2026.10.29.1/XPlane-FSR-Native-0.6.2-platform-preview.1.rar). Includes installation instructions and validation notes.

**Source matching these releases:** [Windows release source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/main) · [NR/FG preview source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/experimental/amd-neural-rendering) · [Earlier FG development source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/experimental/frame-generation) · [Linux/Mac preview source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/experimental/native-platforms/platform) · [All releases](https://github.com/cjboy31/xplane-fsr-vulkan/releases).

Native preview downloads have separate limits: FSR 4, frame generation, VR and
Native AA are unavailable there. Read the selected release's instructions.

## Features

- Corrected camera/depth reconstruction in Windows 0.6.3, with live 12.4.4
  external-pan and powered-cockpit validation. See the
  [diagnosis, tests and remaining limits](docs/ARTIFACT-INVESTIGATION.md).
- Offline narrated/captioned feature guide in Windows 0.6.2: once per installed
  version, immediate Skip, replay from **What's new**, mute and future-video
  opt-out. Includes a real Native AA detail comparison; see the
  [video workflow and limits](docs/FEATURE-VIDEOS.md).
- Frame generation returns to Normal in 0.7.1, with corrected sRGB display
  encoding, safe real-frame holds and optional synchronized compatibility modes.
  NR/FG are Off by default; quality and pacing depend on the workload/display.
- Modern charcoal/champagne native launcher with rounded panels, clear focus
  states, resizing/scrolling, accessible field names and persistent launch/status
  controls. Presets and MSAA/FXAA remain on the main screen.
- **Platform support:** this branch packages Windows x64. The separate
  [native-platform preview](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1)
  offers a Linux FSR 3.1 preview and a Mac diagnostic probe. A Mac Metal FSR
  renderer and live-flight compatibility are not confirmed. See the
  [platform audit and port requirements](docs/PLATFORMS.md).
- Fresh logs for every simulator process, stale camera-frame rejection and
  temporary native rendering/FG pause for a grounded external camera near
  terrain in 0.5.9. See [ground-camera behavior and testing](docs/RENDERING-STABILITY.md).
- Frame-generation source matching/synchronization and safer temporal history
  handling in 0.5.6. See [stability changes and retest steps](docs/RENDERING-STABILITY.md).
- Automatic startup checks for GitHub Releases, preview-channel selection and
  verified one-click installation from **Updates...**, preserving saved choices.
  Install 0.5.4 or later once to enable this feature.
- Selected-installation automatic Vulkan activation for ordinary startup, with
  an optional explicit desktop launch path and a dedicated disable switch.
- FSR 3.1.6 compatibility and 4.1.2 ML-preview AA/upscaling providers.
- FSR frame generation with SDK-controlled presentation in both Windows channels.
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


Use **Updates / Channel > Normal / Experimental** to select future updates.
Starting with 0.7.1, both include all current NR, FG and compatibility options.
Normal is recommended; Experimental receives future development builds.
The app shows installed and selected channels separately and requires an explicit install action to change builds. Recognized installations use
**Switch and install**; extracted copies use **Switch using setup...** with a
verified installer and a folder-selection wizard. See [installation guidance](docs/INSTALLATION.md)
and [release policy](docs/RELEASING.md). Both versioned downloads are listed above;
selecting a channel does not install a build or enable a rendering option.
