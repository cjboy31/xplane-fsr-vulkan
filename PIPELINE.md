# Work pipeline

This file is the project's memory: every idea, question, feature request and bug
report lands here, gets a type and a priority, and moves through the stages
below. Open an [idea or improvement](https://github.com/cjboy31/xplane12-fsr-framegen/issues/new?template=improvement.yml)
or a [bug report](https://github.com/cjboy31/xplane12-fsr-framegen/issues/new?template=bug_report.yml)
and it will be added on the next pass.

## How items are sorted

**Type** (one per item)

- **Feature** – something new the mod can do.
- **QoL** – quality of life: easier install, clearer text, nicer UI.
- **Optimization** – more FPS, steadier frame times and pacing, less memory.
- **Bug** – something that worked or should work and doesn't.

**Priority** – decided by what shipped last, so each update builds on the one before:

| Priority | Rule |
| --- | --- |
| P0 | Bug or regression in the latest Release Candidate or Beta. Fixed before anything else. |
| P1 | Directly extends or finishes the most recent update (the code is fresh and the testers are already looking at it). |
| P2 | New work backed by research or measurements. |
| P3 | Exploratory: needs research or hardware we don't have yet. |

**Stages:** `Queued → In Beta → Verifying → Release Candidate`. An item only moves to
Release Candidate after the checklist in [docs/developer/RELEASING.md](docs/developer/RELEASING.md)
passes. Ties within a priority go to whatever helps the most users (AMD + Intel + NVIDIA,
then Linux, then Mac).

## Shipped in v2 (Betas → waiting for verification)

| Type | Item | Status |
| --- | --- | --- |
| QoL | Hover help cards: point at **?** to read help in place; click pins it; Esc closes | In Beta |
| QoL | Windows installer finds X-Plane 12 automatically (`x-plane_install_12.txt`) and uses plain step-by-step wording | In Beta |
| QoL | Channels renamed **Release Candidates** / **Betas**; saved choice kept | In Beta |
| QoL | Repository renamed `xplane12-fsr-framegen`; old feature-video pipeline and stale docs removed; developer docs moved to `docs/developer/` | In Beta |
| Optimization | Frame pacing profiles (Smooth / Recover after heavy scenes / Power saver) passed to the AMD SDK pacer | In Beta |
| Optimization | Session log checks driver-assisted pacing for this PC (GPU, driver version, present mode/VSync, fullscreen size) and names each blocker | In Test |
| Optimization | **Frame rate cap** for real frames while FG runs: Off / Auto (just under half refresh) / fixed FPS; high-resolution timer + short spin; never bursts after a hitch | In Test |
| QoL | Breathing status lamp while FSR is confirmed active | In Test |
| QoL | Native AA updater follows the Release Candidates / Betas choice (reads `[Updates] Channel`; `beta.windows.N` tags only for Betas; channel shown in its status) | In Test (C# not compiled here) |
| Bug | 0.7.x updaters can't follow the rename: v2 updater follows API redirects and accepts both names | In Test |

## Queued

### Priority: motion vectors

The owner's top rendering item: better motion cuts trails in FSR, shimmer in
Native AA's TAA and warping in frame generation at once. Full plan:
[MOTION-VECTORS-PLAN](docs/developer/MOTION-VECTORS-PLAN.md). Original code only
(*Motion Vectors V2* is GPL-3.0).

| P | Type | Item | Notes |
| --- | --- | --- | --- |
| P1 | Feature | **A** One shared motion contract and producer for FSR, FG and Native AA TAA | **In Test**: contract v1 pinned in both repos; FSR packing, native shader and AA TAA share the GLSL helpers |
| P1 | Feature | **B** Native SSR motion capture as *Motion source: Native (experimental)* | **In Test** (FSR): strict SPIR-V check (MIT-derived), per-frame fallback to camera-derived with the reason logged |
| P1 | Optimization | **C** Vector-quality tool: warp the previous frame, measure error by sky / aircraft / near / far | **In Test**: `XPFSR24_MOTION_CAPTURE` + `tools/motion_quality.py`; numbers needed from a live Camera vs Native capture |
| P2 | Feature | **D** Native motion with MSAA and frame generation | **In Test**: MSAA SSR variant, FG and the cockpit fix; native images copied into the frame's own images |
| P2 | Feature | **E** Per-object motion: traffic, propellers, control surfaces, gear; reactive masks for clouds and glass | **Groundwork in Test**: `XPFSR24_MOTION_SURVEY=1` interface survey; design in [PER-OBJECT-MOTION](docs/developer/PER-OBJECT-MOTION.md). Needs live survey data before building |

### Optimization

| P | Item | Why / evidence | Notes |
| --- | --- | --- | --- |
| P2 | Show the driver-pacing check in the panel, not only the session log | The v2 log line already names each blocker | Needs a status field from the layer to the panel |
| P2 | AMD Anti-Lag 2 through `VK_AMD_anti_lag` when the driver exposes it | Lower input latency with FG; Mesa/RADV now implement it natively on Linux | Must not fight the SDK pacer; measure with PresentMon on Windows |
| P2 | CPU frame time much higher than GPU time (now tracked under *Sim performance and multithreading*) | Owner report; see [CPU-PERFORMANCE](docs/developer/CPU-PERFORMANCE.md) | Compare bypass / FSR / NR / FG in one scene first |
| P2 | Zink audit: run the layer with `--zink` and `--no_zink` and confirm no false bypass | Laminar plans Zink as the only plugin GL path | Native AA already fixed a Zink push-descriptor false bypass (0.3.3) |
| P3 | Second-GPU frame generation (Lossless Scaling style) | Lossless Scaling renders on GPU A and generates on GPU B with the monitor on GPU B; PCIe bandwidth is the limit | See [MULTI-GPU-DESIGN](docs/developer/MULTI-GPU-DESIGN.md). 4K RGBA16F at 60 FPS ≈ 3.7 GiB/s one way |
| P3 | Warm shader cache with progress indicator | Fewer first-use stalls | Measure cold vs warm first |

### Sim performance and multithreading (exploration)

X-Plane 12.4 already spreads scenery traversal and avionics across cores, but its
main thread still records all rendering into one command buffer, and that is the
buffer our layer works inside. Plan and validation rules:
[SIM-PERFORMANCE](docs/developer/SIM-PERFORMANCE.md).

| P | Item | Why | Notes |
| --- | --- | --- | --- |
| P1 | Per-frame timers for the layer's own CPU work on X-Plane's render thread, shown in Diagnostics next to X-Plane's frame time | Measure before optimizing; answers the "CPU time much higher than GPU time" report | Separates capture, record, submit and waits |
| P2 | Move remaining mod CPU work off X-Plane's render thread (workers, pre-built descriptors) | Every ms we add there is sim frame time | Only where the P1 timers show it matters |
| P2 | Opt-in CPU scheduling helper (Windows): keep X-Plane off E-cores on Intel hybrid and on the V-cache CCD of dual-CCD X3D; no power throttling | Steadier main-thread frame time on those CPUs | Off by default, restored at exit, measured per CPU type; little effect expected on single-CCD CPUs |
| P2 | Pipeline/shader warm-up on a worker thread with a validated cache | Removes first-use hitches | Reject caches after driver changes |
| P3 | FSR / NR (then FG) on an async compute queue | Overlap our GPU work with X-Plane's shadow and scene passes | After P1 shows GPU overlap room; SDK async FG is fixture-only so far |

### Feature

| P | Item | Why / evidence | Notes |
| --- | --- | --- | --- |
| P2 | FSR 4.1 on more AMD hardware | SDK 2.4.0 adds FSR 4.1 on RX 6000 (RDNA 2) and Ryzen AI Max / AI 9 APUs | README and hardware guide already state it; needs a real RX 6000 test |
| P2 | **v3: one mod** for FSR, Native AA, frame generation and neural rendering, with Native AA + FG as a new combination | One panel, installer, updater and layer; see [V3-MERGE](docs/developer/V3-MERGE.md) | Starts after v2 passes Betas; five steps, each through Test → Betas → RC. Native AA never needs AMD files |
| P3 | Intel XeSS upscaling as an extra choice on Intel Arc | XeSS upscaling supports Vulkan; XeSS frame generation and XeLL are DX12-only, so they can't run in X-Plane | User would supply Intel's runtime, like the NR model |
| P3 | Linux: FSR 4 / FG parity with Windows | Linux preview only has FSR 3.1 upscaling | Needs Linux AMD providers and real flights |
| P3 | Mac: MetalFX backend | X-Plane uses Metal on Mac, so a Vulkan layer never sees its frames | A new Metal integration; the current Mac download is diagnostics only |

### QoL

| P | Item | Notes |
| --- | --- | --- |
| P1 | Linux one-click installer that finds X-Plane via `~/.x-plane/x-plane_install_12.txt` | Mirror the Windows auto-detect |
| P1 | Mac installer that clearly says "diagnostics only" and finds X-Plane via `~/Library/Preferences/x-plane_install_12.txt` | Never imply Mac rendering works |
| P2 | Remove the retired intro-video code from the desktop updater | Deferred: it touches updater startup and five tests for little user benefit; do it with a Windows test run |
| P2 | More panel motion: animated section reveal | Status lamp pulse shipped; keep `SPI_GETCLIENTAREAANIMATION` respected |

### Bugs

| P | Item | Notes |
| --- | --- | --- |
| P0 | 0.5.4-0.7.10 updaters can't find updates after the rename | They reject GitHub's API redirect and check the old name. v2 updater fixed (follows the redirect, accepts both names). Existing users need one manual v2 install; say so in README, release notes and the Discord post. Optional: a 0.7.11 hotfix cannot help, since those users can't reach it either. |

Other live bugs: Live bugs are tracked in [GitHub issues](https://github.com/cjboy31/xplane12-fsr-framegen/issues?q=label%3A%22pipeline%3A+bug%22).

## Research notes that shaped this list

Kept short on purpose; the full write-up is in the v2 research report.

- **AMD FSR SDK 2.4.0 (Oct 28, 2026):** FG swapchain 4.0.0 with driver-assisted pacing on RDNA 4; pacing tuning key on Vulkan; FSR 4.1 on RDNA 2 and more APUs.
- **Intel XeSS 2.1:** FG and XeLL run on AMD/NVIDIA too, but only on DirectX 12.
- **LuminaXP:** closed source, NVIDIA-focused (DLSS 5 / DLAA / FG / VR). Useful as a behavior reference only: early implicit-layer activation, separate real vs generated frame counters, rejecting late generated frames.
- **Laminar:** Zink converts plugin OpenGL into Vulkan on the sim's device and is planned to be the only option; don't call X-Plane before it finishes loading; AMD device losses are being investigated with Aftermath, so the layer must bypass rather than risk a loss.
- **ReShade:** reports of X-Plane 12 Vulkan crashes when an aircraft loads; not a dependency for either mod.
