<img src="assets/app-icon.png" width="96" alt="FSR aircraft icon">

# FSR 3.1 / 4.1 + Frame Gen for X-Plane 12

Choose smoother edges, upscaling, frame generation and optional neural rendering
from a panel inside X-Plane. This is a community mod, not an official AMD or
Laminar Research product.

## Download and setup

[Windows Release — recommended](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.9)
· [Windows Beta](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/v0.7.9-beta.windows.1)
· [All releases](https://github.com/cjboy31/xplane-fsr-vulkan/releases)
· [Join Discord](https://discord.gg/yh396QNgHZ)

1. **Install:** close X-Plane. Run the file ending in **Setup.exe**, select your
   X-Plane folder and enable **in-sim integration**. The other EXEs are support tools.
2. **Open settings:** start X-Plane normally and load a flight. At the top of the
   simulator, open **Plugins > FSR 3.1 / 4.1 + Frame Gen > Open settings**.
3. **Find presets:** on **Rendering**, open **Resolution preset** and choose one.
4. **Apply:** open **Image quality**, scroll down and select
   **Apply graphics after I close X-Plane**. Close X-Plane normally, then reopen it.
5. **Check:** load a flight and reopen the panel. Look for **FSR ACTIVE**.
   Read any waiting or bypass message if it is not active.

**The presets are inside X-Plane.** The desktop **Plugin Updater** only installs
updates. Start with FXAA, MSAA, frame generation and NR Off. Use **Full** input
with **Native AA**, or **Quality** input with **Upscaling**.
See the [step-by-step setup and preset instructions](docs/INSTALLATION.md).

## Features

- **FSR 3.1.6 and 4.1.2 Native AA:** smooth edges at full input resolution.
- **Upscaling:** build a larger picture from a smaller input. Lower inputs can improve speed but lose fine detail.
- **Frame generation 3.1.6 and 4.0.2:** add frames for smoother displayed motion. It does not speed up the simulation. Optional compatibility modes are included.
- **Neural rendering:** adjust lighting, color and detail live. Choose outside-only or whole-scene use, and whether it runs before or after upscaling.
- **Resolution and cloud presets:** choose a starting point, then adjust image quality. Changes marked for restart apply after you close X-Plane normally.
- **Cockpit screen protection:** restore supported display areas after FSR. It needs frame generation Off and does not support every screen layout. Turn it off if alignment looks wrong.
- **Saved settings and live status:** choices are remembered, and the panel shows whether image processing is working.
- **One image processor at a time:** when independent Native AA is also installed, choose the mod you want and restart. Both mods keep their choices.

NR needs compatible Windows RDNA4 FP8 hardware and your own supported model.
NVIDIA DLLs and model weights are not included. Use one NR pass to begin; extra
passes can blur textures and cost more graphics time. More smoothing is not
always clearer. Visual results vary by aircraft, view, graphics card and display.

## Recommended hardware and settings

For a comfortable starting point, we suggest Windows 11, a recent CPU with
strong performance per core, 32 GB RAM, an SSD, and a graphics card with
12 GB VRAM for 1440p or 16 GB for 4K. These are project setup suggestions,
not tested minimums or a guarantee of FPS. Heavy aircraft and scenery need more.
Our main Windows test GPU is the Radeon AI PRO R9700; other GPUs need their own checks.

Start with **Quality upscaling**, **FXAA Off**, **MSAA Off**, **NR Off** and
**frame generation Off**. For Native AA, use **Full** input instead. NR changes
the picture and can lower FPS; it is optional. If you add it, start with the
Small model, one pass and Balanced 50%, before upscaling.

Read the [hardware, GPU compatibility and low-FPS guide](docs/HARDWARE-AND-SETTINGS.md)
for FSR 3.1 versus 4.1, frame-generation requirements, resolution examples and
how to find what is slowing your sim down. Linux and Mac limits are listed too.

## Updates and help

Choose **Release** for all current features or **Beta** to try upcoming changes.
The previous Experimental updates are included in Release. Your saved Normal
choice maps to Release; Experimental maps to Beta. Version 0.7.9 keeps the
installer, buffer-reuse and startup corrections from 0.7.7 and 0.7.6.
Diagnostics provides easy access to fresh session logs. The grouped controls,
help and transitions from 0.7.5 are retained.
Optional [diagnostic reporting](docs/DIAGNOSTIC-REPORTS.md) lets you preview a
filtered bundle and send it to Discord now or after this session. It starts Off.
The bundle can include X-Plane Log.txt, startup errors, a saved settings summary
and small crash text. Original logs, models and preference files stay on your PC.
The separate **Plugin Updater** handles updates for FSR and Native AA. Open **Changelog**
inside the simulator to read changes; startup videos are retired. An optional
[shadow script](extras/shadows/README.txt) is included separately and starts Off.
Read the instructions included with your download.

[Setup instructions](docs/INSTALLATION.md) ·
[Report a problem](https://github.com/cjboy31/xplane-fsr-vulkan/issues) ·
[Upload diagnostic logs](https://xplane-mod-log-reports.cjboy31.chatgpt.site/) ·
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
