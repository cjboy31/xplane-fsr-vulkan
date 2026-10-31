<img src="assets/app-icon.png" width="96" alt="FSR aircraft icon">

# FSR 3/4 + Frame Gen for X-Plane 12

Choose smoother edges, upscaling, frame generation and optional neural rendering
from a panel inside X-Plane. This is a community mod, not an official AMD or
Laminar Research product.

## Download and setup

[Windows Normal — recommended](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.3-alpha)
· [All releases](https://github.com/cjboy31/xplane-fsr-vulkan/releases)
· [Join Discord](https://discord.gg/yh396QNgHZ)

Close X-Plane, run setup, select your simulator folder and enable in-sim integration.
Use your usual launcher afterward. Open **Plugins > FSR 3/4 + Frame Gen** for settings.
Keep X-Plane's FXAA Off. Use Full input for Native AA, or a lower input setting for
upscaling. Start with frame generation and neural rendering Off.

## Features

- **FSR 3 and FSR 4 Native AA:** smooth edges at full input resolution.
- **Upscaling:** build a larger picture from a smaller input. Lower inputs can improve speed but lose fine detail.
- **Frame generation:** add frames for smoother displayed motion. It does not speed up the simulation. Optional compatibility modes are included.
- **Neural rendering:** adjust lighting, color and detail live. Choose outside-only or whole-scene use, and whether it runs before or after upscaling.
- **Resolution and cloud presets:** choose a starting point, then adjust image quality. Changes marked for restart apply after you close X-Plane normally.
- **Cockpit screen protection:** restore supported display areas after FSR. It needs frame generation Off and does not support every screen layout. Turn it off if alignment looks wrong.
- **Saved settings and live status:** choices are remembered, and the panel shows whether image processing is working.
- **One image processor at a time:** when independent Native AA is also installed, choose the mod you want and restart. Both mods keep their choices.

NR needs compatible Windows RDNA4 FP8 hardware and your own supported model.
NVIDIA DLLs and model weights are not included. Use one NR pass to begin; extra
passes can blur textures and cost more graphics time. More smoothing is not
always clearer. Visual results vary by aircraft, view, graphics card and display.

## Updates and help

The next UI package replaces the desktop rendering dashboard with a **Plugin
Updater**. Rendering settings stay in X-Plane. An in-sim **Changelog** replaces
the startup video. Existing 0.7.3 downloads keep their original interface until
updated. Read the instructions included with your download.

[Setup instructions](docs/INSTALLATION-0.7.3.txt) ·
[Report a problem](https://github.com/cjboy31/xplane-fsr-vulkan/issues) ·
[Discord support](https://discord.gg/yh396QNgHZ)

## Platforms

Windows is the current rendering package. The separate
[Linux preview](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1)
only offers FSR 3.1 upscaling and still needs Linux flight testing. It does not
include FSR 4, NR or frame generation. The Mac download is a diagnostic tool;
Mac rendering is not available.

For developers: [Build](BUILDING.md) · [Testing](docs/RENDERING-STABILITY.md) ·
[Contribute](CONTRIBUTING.md). Original code and artwork are MIT; supplied
[third-party licenses](licenses/) have their own terms.
