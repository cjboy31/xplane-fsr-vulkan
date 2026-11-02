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
| QoL | Saved live frame counter: real simulator FPS and extra generated FPS, with a panel toggle and plugin-menu shortcut | In Test; Windows live On/Off, FG Off/On, help and restart checks passed on `6041659`; see [validation](docs/developer/FRAME-COUNTER-VALIDATION.md) |
| Bug | Bound source images awaiting SDK presenter completion; drain pending images even after memory fallback | In Test; corrects measured 95-frame / 18,596 MiB retention in the ordinary presenter. A 20-minute Windows session on `6041659` kept sampled SDK-owned source images bounded; full promotion checklist remains open |
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

**Pass of 2026-11-02:** every P1-P3 item that can be built and checked without
the owner's PC is now In Test. The rest wait on something specific:

| Waiting on | Items |
| --- | --- |
| A live flight on the owner's PC | P2 Zink audit (`--zink` / `--no_zink`); P2 CPU vs GPU time comparison (bypass / FSR / NR / FG, now readable in Diagnostics) |
| Live timer numbers (TEST-CHANNEL check 14) | P2 move remaining CPU work to workers; P3 async compute (needs proof of GPU overlap room) |
| Live survey data (check 12) | P2 E per-object motion |
| Hardware we don't have | P2 FSR 4.1 on RX 6000 / Ryzen AI; P3 second-GPU FG; P3 XeSS (Intel Arc + Intel's runtime) |
| AMD Linux providers | P3 Linux FSR 4 / FG parity |
| A separate Metal project | P3 Mac MetalFX |
| v2 passing Betas | P2 v3 one-mod merge |

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

### Laminar's updates and the rest of the PC

Research: [LAMINAR-AND-HARDWARE](docs/developer/LAMINAR-AND-HARDWARE.md) (12.4.4 SDK Update, 12.5, tile scenery engine; what hardware can and cannot do for the sim).

| P | Type | Item | Notes |
| --- | --- | --- | --- |
| P1 | Bug | Verify on X-Plane 12.4.4 final (Oct 29): SSR classifier (native motion), FSR pass replacement, cockpit-screen restore, FG, Zink 25.3.4, sparse-residency device creation on AMD Linux | TEST-CHANNEL check 21. 12.4.4 changed lighting, texture residency, FSR LOD bias and panel drawing |
| P2 | Feature | **Texture detail**: AMD's temporal-upscaler mip bias (`log2(render/display) - 1`) on top of X-Plane's own | **In Test**: opt-in panel row; only anisotropic material samplers; device-limit clamp; logs X-Plane's original values. Check 20 |
| P2 | Optimization | Move the in-sim panel to the **Panel Graphics API** (Vulkan, batched, MSDF text) | Laminar: OpenGL plugin drawing "requires expensive bridging and is impossible to make multi-core". Needs SDK 4.4 final (RC1 now); keep the OpenGL path for older X-Plane |
| P2 | Feature | NVIDIA low latency (`VK_NV_low_latency2`, Reflex for Vulkan), mirroring Anti-Lag 2 | FG and FSR run on NVIDIA too; opt-in, only when the driver exposes it |
| P2 | Bug | Panel Graphics cockpit displays: confirm cockpit-screen restore finds screens drawn by X-Plane's Vulkan avionics path | Needs a 12.4.4 aircraft that uses Panel Graphics |
| P3 | Optimization | Tile scenery engine (late 2026): re-check pass identification on its first beta | Both mods bypass on unknown passes, so the risk is fallback, not corruption |

### Optimization

| P | Item | Why / evidence | Notes |
| --- | --- | --- | --- |
| P2 | Show the driver-pacing check in the panel, not only the session log | The v2 log line already names each blocker | **In Test**: Diagnostics *Driver pacing* row (telemetry `pacingCheck`); lists each blocker or says the requirements look met, never that pacing is active |
| P2 | AMD Anti-Lag 2 through `VK_AMD_anti_lag` when the driver exposes it | Lower input latency with FG; Mesa/RADV now implement it natively on Linux | **In Test**: opt-in *Anti-Lag 2* row (INI `AntiLag`, env `XPFSR24_ANTI_LAG`). Extension + feature enabled only when offered; present marker before each real frame, input marker right after it; maxFPS 0 so the cap and SDK pacer keep timing; SDK presenter traffic untouched. Diagnostics shows On or the reason. Needs PresentMon latency On vs Off, with and without FG |
| P2 | CPU frame time much higher than GPU time (now tracked under *Sim performance and multithreading*) | Owner report; see [CPU-PERFORMANCE](docs/developer/CPU-PERFORMANCE.md) | Compare bypass / FSR / NR / FG in one scene first |
| P2 | Zink audit: run the layer with `--zink` and `--no_zink` and confirm no false bypass | Laminar plans Zink as the only plugin GL path | Native AA already fixed a Zink push-descriptor false bypass (0.3.3) |
| P3 → **owner priority** | Second-GPU frame generation (Lossless Scaling style) | Lossless Scaling renders on GPU A and generates on GPU B with the monitor on GPU B; PCIe bandwidth is the limit | **In Test (stage 1)**: *Frame generation GPU: Second GPU*. X-Plane's swapchain becomes virtual on the render GPU; each real frame plus FSR's depth and motion goes through host memory to a private device on the display GPU, which runs AMD's FG swapchain (or, if it cannot, shows real frames). Falls back to single-GPU FG on any set-up failure. Next: zero-copy host import (`VK_EXT_external_memory_host`), HDR, measured PCIe numbers. See [MULTI-GPU-DESIGN](docs/developer/MULTI-GPU-DESIGN.md); TEST-CHANNEL check 23 |
| P3 | Warm shader cache with progress indicator | Fewer first-use stalls | **In Test**: preparation already moved off the render thread (P2); the panel status now reads *FSR GETTING READY - preparing in the background (N s)* while it runs. Cold vs warm times come from the log's `FSR context preparation: N ms` (check 17: first start after a driver update vs a second start) |

### Sim performance and multithreading (exploration)

X-Plane 12.4 already spreads scenery traversal and avionics across cores, but its
main thread still records all rendering into one command buffer, and that is the
buffer our layer works inside. Plan and validation rules:
[SIM-PERFORMANCE](docs/developer/SIM-PERFORMANCE.md).

| P | Item | Why | Notes |
| --- | --- | --- | --- |
| P1 | Per-frame timers for the layer's own CPU work on X-Plane's render thread, shown in Diagnostics next to X-Plane's frame time | Measure before optimizing; answers the "CPU time much higher than GPU time" report | **In Test**: Diagnostics *PERFORMANCE PER FRAME* shows mod work, mod waits (FG drains), present call and frame interval (2 s averages, telemetry ABI 5) beside X-Plane's frame and GPU time; same numbers in the log (`per-frame-us:`). TEST-CHANNEL check 14 |
| P2 | Move remaining mod CPU work off X-Plane's render thread (workers, pre-built descriptors) | Every ms we add there is sim frame time | Only where the P1 timers show it matters |
| P2 | Opt-in CPU scheduling helper (Windows): keep X-Plane off E-cores on Intel hybrid and on the V-cache CCD of dual-CCD X3D; no power throttling | Steadier main-thread frame time on those CPUs | **In Test**: *CPU scheduling* row (Frame generation section; INI `CpuScheduling`, env `XPFSR24_CPU_SCHEDULING`). Process default CPU sets + EcoQoS off for X-Plane only; cleared when the renderer device closes; Diagnostics shows what it did. Plan logic unit-tested; needs 1% lows On vs Off on a hybrid and a dual-CCD X3D CPU |
| P2 | Pipeline/shader warm-up on a worker thread with a validated cache | Removes first-use hitches | **In Test**: the FSR context (the AMD SDK's pipelines and resources, the big first-use and resize cost) is now prepared on a worker thread; X-Plane's own image shows until it is ready. No cache file: our own seven compute pipelines are tiny and the driver already caches the SDK's, so a file would add risk without a measurable gain |
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
| P1 | Linux one-click installer that finds X-Plane via `~/.x-plane/x-plane_install_12.txt` | **In Test** on `experimental/native-platforms` (e95e3ab): launcher fills the folder when none is saved and labels it "found automatically" |
| P1 | Mac installer that clearly says "diagnostics only" and finds X-Plane via `~/Library/Preferences/x-plane_install_12.txt` | **In Test** on the same branch: same detection; the Mac screen and guide still say diagnostics only |
| P2 | Remove the retired intro-video code from the desktop updater | **In Test**: player, policy, its tests, the legacy launcher's "Watch feature video" button and timer removed (the shipped `plugin_updater.cpp` never used them). Installer still retires old guide videos for upgraders. Windows CI runs the launcher tests |
| P2 | More panel motion: animated section reveal | **In Test**: on a tab change rows fade in top to bottom (≤160 ms stagger, 6 px rise); off when Windows animations are off; hit areas unchanged |

### Bugs

| P | Item | Notes |
| --- | --- | --- |
| P0 | 0.5.4-0.7.10 updaters can't find updates after the rename | They reject GitHub's API redirect and check the old name. v2 updater fixed (follows the redirect, accepts both names). Existing users need one manual v2 install; say so in README, release notes and the Discord post. Optional: a 0.7.11 hotfix cannot help, since those users can't reach it either. |
| P0 | Airborne camera reset on every frame (live test 2026-11-02: 709/709 climb frames reset=1; ground 0) | **Fixed in Test**: the plugin's fixed clip-space bound (100) grows with step / near plane, so forward flight crossed it every frame. Now only unusable matrices reset; real cuts stay physical (`cameraCut`). Regression test in `core_tests`. Live recheck: airborne reset count should fall to near 0 |
| P0 | Esc did not close a pinned help card and opened X-Plane's menu | **Fixed in Test**: key sniffer consumes Esc (and its key-up) only while a dropdown or pinned card is open; all other keys pass through. Same fix in Native AA |
| P0 | Uninstall deleted saved choices (`settings.ini`), against the checklist | **Fixed in Test**: uninstall keeps `settings.ini` and the folder marker; reinstalling into the same folder restores choices (installer test extended) |
| P1 | FG costs ~40% of the real frame rate on the owner's PC (40 → 24 FPS) | The owner's method is *FSR FG 4.0.2 synchronized*, which drains every present on the CPU by design. The log now reports `sync-drains` and their average/max time. Next live step: same flight with *FSR FG 4.0.2* (SDK paced) to separate drain cost from interpolation cost; the panel help now explains the trade-off |

Other live bugs: Live bugs are tracked in [GitHub issues](https://github.com/cjboy31/xplane12-fsr-framegen/issues?q=label%3A%22pipeline%3A+bug%22).

## Research notes that shaped this list

Kept short on purpose; the full write-up is in the v2 research report.

- **AMD FSR SDK 2.4.0 (Oct 28, 2026):** FG swapchain 4.0.0 with driver-assisted pacing on RDNA 4; pacing tuning key on Vulkan; FSR 4.1 on RDNA 2 and more APUs.
- **Intel XeSS 2.1:** FG and XeLL run on AMD/NVIDIA too, but only on DirectX 12.
- **LuminaXP:** closed source, NVIDIA-focused (DLSS 5 / DLAA / FG / VR). Useful as a behavior reference only: early implicit-layer activation, separate real vs generated frame counters, rejecting late generated frames.
- **Laminar:** Zink converts plugin OpenGL into Vulkan on the sim's device and is planned to be the only option; don't call X-Plane before it finishes loading; AMD device losses are being investigated with Aftermath, so the layer must bypass rather than risk a loss.
- **ReShade:** reports of X-Plane 12 Vulkan crashes when an aircraft loads; not a dependency for either mod.
