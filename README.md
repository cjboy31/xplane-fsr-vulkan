<img src="assets/app-icon.png" width="96" alt="FSR aircraft icon">

# FSR Upscaling + Frame Gen for X-Plane 12

Sharper edges, higher FPS and smoother motion in X-Plane 12, controlled from a
panel inside the simulator. Uses AMD FSR 3.1 / 4.1 upscaling and anti-aliasing,
AMD frame generation, and optional neural rendering.

*A community mod. Not made by AMD or Laminar Research.*

## Download

| Channel | Who it's for | Link |
| --- | --- | --- |
| **Release Candidates** | Everyone. Only tested and verified features. | [Latest Release Candidate](https://github.com/cjboy31/xplane12-fsr-framegen/releases/latest) |
| **Betas** | People who want to try new features early and report problems. | [All releases (Betas are marked *Pre-release*)](https://github.com/cjboy31/xplane12-fsr-framegen/releases) |

Windows is the main platform. See [Linux and Mac](#linux-and-mac) below.

**Updating from 0.7.x?** This project was renamed, and the 0.7.x updater can't
follow the new name. Download and run the new Setup.exe once; your saved settings
are kept. From 2.0 on, updates work from the updater again.

## Install in 3 steps (Windows)

1. **Close X-Plane.** Download the file ending in **`-Setup.exe`** and run it.
2. Setup finds X-Plane 12 for you. Check the folder, keep
   **Load the mod automatically when X-Plane starts** ticked, then click **Install**.
3. Start X-Plane the way you always do and load a flight. Open
   **Plugins → FSR 3.1 / 4.1 + Frame Gen → Open settings**. You should see
   **FSR ACTIVE** at the top.

Point at any **?** in the panel to read what a setting does. Click the **?** to
keep the card open.

## First settings that work well

1. **Rendering** tab → **Resolution preset** → pick the one that matches your monitor.
2. **Image quality** tab → scroll down → **Apply graphics after I close X-Plane**.
3. Close X-Plane normally and start it again.

Start with frame generation, neural rendering, FXAA and MSAA **Off**. Use
**Quality** upscaling for more FPS, or **Native AA** if you already have enough FPS
and just want cleaner edges. More help: [step-by-step setup](docs/INSTALLATION.md) ·
[hardware and low-FPS guide](docs/HARDWARE-AND-SETTINGS.md).

## What it does

- **FSR Native AA (3.1.6 / 4.1.2):** smoother edges at your full resolution.
- **FSR Upscaling:** X-Plane draws a smaller picture and FSR rebuilds it at full
  size. More FPS; very low settings lose fine detail.
- **Frame generation (3.1.6 / 4.0.2):** adds in-between frames so motion looks
  smoother. It does not make the simulation itself run faster.
- **Frame pacing** *(new in v2, Beta)*: choose **Smooth**, **Recover after heavy
  scenes** or **Power saver** for how generated frames are timed.
- **Neural rendering (optional):** live lighting, color and detail adjustments.
  Needs a compatible AMD RDNA 4 card and a model file you supply yourself.
- **Cockpit screen protection:** restores supported cockpit displays after FSR
  (frame generation must be Off).
- **Hover help** *(new in v2, Beta)*: help appears right where you point.
- **One image processor at a time:** if the separate
  [Native AA mod](https://github.com/cjboy31/make-xp-anti-aliasing-great-again)
  is also installed, pick one on the **Rendering** tab and restart. Both keep your settings.

### Smoothest frame generation

- Keep your real frame rate a little **below half** your monitor's refresh rate
  (for example under 72 FPS on a 144 Hz screen).
- Use **fullscreen** or a **borderless window that fills the screen**.
- Turn **VSync off** and **FreeSync / variable refresh on**.

On Radeon RX 9000 cards with Adrenalin 26.10.1 or newer, AMD's driver can then
time the frames for you, which is smoother still.

## Recommended hardware

Windows 11, a recent CPU with strong single-core speed, 32 GB RAM, an SSD and a
graphics card with 12 GB of video memory for 1440p or 16 GB for 4K. These are
suggestions, not guarantees. Main test card: AMD Radeon AI PRO R9700.

Supported by AMD's SDK (only AMD cards have been tested with this mod so far; Intel and NVIDIA reports are welcome):

| Brand | What can run |
| --- | --- |
| AMD | Everything. FSR 4.1 and FG 4.0 need RX 9000 series (AMD's SDK also lists FSR 4.1 for RX 6000/7000). Neural rendering needs RDNA 4. |
| Intel Arc | FSR 3.1 upscaling, Native AA and FSR 3.1 frame generation. |
| NVIDIA | FSR 3.1 upscaling, Native AA and FSR 3.1 frame generation. |

## Updates

The in-sim panel has a **Changelog** button, and the separate **Plugin Updater**
installs new versions of this mod and Native AA. Pick **Release Candidates** or
**Betas** there; your choice is remembered.

## Linux and Mac

- **Linux:** a separate [Linux preview](https://github.com/cjboy31/xplane12-fsr-framegen/releases/tag/native-preview-2026.10.29.1)
  offers FSR 3.1 upscaling only (no FSR 4, frame generation or neural rendering yet).
- **Mac:** X-Plane uses Apple's Metal graphics on Mac, which this mod can't hook
  into yet. The Mac download is a diagnostics tool only; it does not change the picture.
  See [platform details](docs/PLATFORMS.md).

## Help

[Report a problem](https://github.com/cjboy31/xplane12-fsr-framegen/issues/new?template=bug_report.yml) ·
[Suggest an idea](https://github.com/cjboy31/xplane12-fsr-framegen/issues/new?template=improvement.yml) ·
[Upload diagnostic logs](https://xplane-mod-log-reports.cjboy31.chatgpt.site/) ·
[Discord](https://discord.gg/yh396QNgHZ) ·
[What we're working on](PIPELINE.md)

In the panel, **Diagnostics → Open log folder** shows your logs.
[Optional diagnostic reporting](docs/DIAGNOSTIC-REPORTS.md) can send a filtered
bundle to Discord; it starts Off.

An optional [shadow script](extras/shadows/README.txt) is included separately and starts Off.

---

**For developers:** [Build](BUILDING.md) · [Architecture](docs/developer/ARCHITECTURE.md) ·
[Releasing](docs/developer/RELEASING.md) · [Contributing](CONTRIBUTING.md) ·
[All developer notes](docs/developer/).
Original code and artwork are MIT. Supplied [third-party licenses](licenses/) have their own terms.
NVIDIA DLLs and model weights are never included.
