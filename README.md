<img src="assets/app-icon.png" width="96" alt="Original aircraft and temporal-pixel icon">

# FSR 3/4 + Frame Gen for X-Plane 12

An experimental Windows Vulkan integration for AMD FSR Native AA, upscaling and
frame generation, with a new current-frame cockpit-screen compositor for the
default Cirrus SR22. Original project code and artwork are MIT licensed.

**Developer preview.** This project is being opened for community development.
GPU fixtures pass on a Radeon AI PRO R9700; the new SR22 screen projection and
visual quality still need live-flight validation. It is not an official AMD or
Laminar Research product, and is not a complete solution to ghosting or stutter.

## Downloads and current source

- **Windows:** [0.6.2-alpha installer and instructions](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.6.2-alpha). Built-in frame generation is suspended in regular releases.
- **Feature video:** [Watch or download the 100-second narrated guide](https://github.com/cjboy31/xplane-fsr-vulkan/releases/download/v0.6.2-alpha/XPlane-FSR3-4-0.6.2-alpha-Feature-Guide.mp4). Fresh real Native AA comparison, setup help, captions and immediate Skip in the Windows launcher. [Versioned-video workflow](https://github.com/cjboy31/xplane-fsr-vulkan/blob/codex/saved-presets-and-status/docs/FEATURE-VIDEOS.md).
- **Linux:** [Native FSR 3.1 preview downloads](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1) — requires MSAA off and FSR supersampling enabled; real-flight validation is pending.
- **Mac:** [Universal compatibility-probe download](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1) — diagnostics only; **Mac FSR/Metal rendering is not implemented**.
- **Combined Linux/Mac RAR:** [Download the native preview package](https://github.com/cjboy31/xplane-fsr-vulkan/releases/download/native-preview-2026.10.29.1/XPlane-FSR-Native-0.6.2-platform-preview.1.rar). Includes installation instructions and validation notes.

**Source matching these releases:** [Windows release source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/codex/saved-presets-and-status) · [Linux/Mac preview source](https://github.com/cjboy31/xplane-fsr-vulkan/tree/experimental/native-platforms/platform) · [All releases](https://github.com/cjboy31/xplane-fsr-vulkan/releases).

The repository opens on `main`, which contains the original implementation snapshot.
Current release development is on the branches linked above; the native preview is
also labeled **Pre-release** in Releases. Its FSR 4, frame-generation, VR and Native AA
features are unavailable. Read the selected release's instructions and limitations.

## Features

- Explicit Vulkan layer enabled only for X-Plane started by the launcher.
- FSR 3.1.6 compatibility and 4.1.2 ML-preview AA/upscaling providers.
- Built-in frame generation is suspended in current regular releases; development stays experimental.
- Seven resolution presets configuring output, internal scale and AA settings,
  with backups and selective restoration.
- An independent Vulkan cockpit compositor: replays current-scene screen pixels
  after temporal reconstruction, with jitter correction, depth checks, edge
  erosion and an alignment tint. Initial target: default Cirrus SR22 PFD/MFD.
- Camera-pose stability checks and a separate IPC ABI for the new publisher.
- CPU source-pacing diagnostics and isolated AA/FG/cockpit GPU tests.

The cockpit compositor currently requires **frame generation Off**. It restores
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
