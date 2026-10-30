# Independent neural rendering on AMD — experimental

Development branch: `experimental/amd-neural-rendering`. Windows Vulkan only.
NR is off by default and separate from FSR AA/upscaling. It changes scene detail
and lighting and adds GPU work. No official NVIDIA integration, performance
gain or absence of artifacts is claimed.

## References and reuse

The implementation compiles the Windows Vulkan core and shaders from
[mochizuki0323/DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD), pinned at
`960038fd1cb892742548f408355e0f3b2fd30d6f`, under MIT. Build-NR.ps1 rejects a
different commit or tracked changes and builds the model extractor from source.
Driver-compiled SPIR-V is used; ACO machine-code importing and upstream
environment overrides are disabled. ReShade, OptiScaler, NGX, D3D, DXVK,
MinHook and modified-loader routes are not used. Upstream MIT, FSR1 MIT shader
and component notices are preserved.

The additional reference is **DLSS-NR-on-AMD 0.8.0**, replacing 0.6.0. Its local
folder contains documentation and a license restricting copying, redistribution
and derivative use, without implementation source. Its
[release notes](https://github.com/danielblnc/DLSS-NR-on-AMD/releases/tag/v0.8.0)
describe modular controls, centralized user-model import and pre-upscale
temporal NR. These are design/test references only; no code, runtime or assets
are copied. Its D3D12 SR/RR/MFG and RDNA3 features do not establish support in
this Vulkan prototype.

NVIDIA's DLL and extracted weights remain the user's local assets. The DLL is
read as data and never executed. Neither DLL nor weights are committed,
included in installer manifests or uploaded. Only known model sizes and
SHA-256 identities are accepted.

## First test

1. Close X-Plane. Open **NR settings / Model** and import your supported
   `nvngx_dlssnr.dll` 310.9.16 or 310.8.0. The importer checks each tensor; the
   launcher and backend also verify the complete selected model hash.
2. Run **NR check**. It processes small, fenced synthetic HDR frames in a
   separate process, without launching or modifying X-Plane.
3. Choose matching FSR Native AA/upscale settings, FXAA Off, MSAA Off, FG Off,
   and **NR Subtle**. Keep current output or deliberately choose a preset.
   Launch here and load a flight.
4. Enter an external view above the ground-contact safety range. Read the NR
   status: preparing, recording, waiting, protected and failed states differ.
   Model/pipeline preparation happens in a background worker; first compilation
   may take tens of seconds.
5. Compare the same scene before increasing strength. On/Off, artistic controls,
   style, history, passes and scene scope apply live. Model/resolution changes
   prepare in the background while FSR continues. Saved NR choices survive
   reopening. Importing model files still requires X-Plane closed.

Do not install another mod's Vulkan proxy into this application's NR folder.
This adapter uses the system loader and the application's explicit layer.

## Rendering contract and limits

AMD RDNA4 FP8 E4M3 cooperative matrices, 32-lane compute subgroups, FP16/INT8
storage, buffer device address and workgroup explicit layout are required.
Test hardware: Radeon AI PRO R9700. Other GPUs/drivers need their own check.
Linux NR and Mac NR are not integrated by this branch.

The layer enables features on its private device-chain clone, preserving
aggregate aliases and the app's other features. Core Vulkan 1.4 pipeline
robustness is accepted without requiring the older extension name. The runtime
adopts the simulator's device/graphics queue with synchronized background
uploads. A private copy of linear pre-FSR color receives NR, then transitions
to shader-read layout for FSR. Original FSR input is untouched. Both histories
reset at NR transitions. Default model resolution is 50%, one pass and 25%
overall intensity, with conservative color/detail controls. Performance,
Balanced and Quality budgets select 33/50/67%, prefer the small network and
retain artistic strengths. Advanced choices include 25/100% and up to three
passes; additional passes increase GPU work substantially.

Motion is estimated from images on the GPU, not exact engine/object/cloud
motion. **External views only** excludes the cockpit by default. **Full scene**
includes cockpit and exterior processing; switching scope resets temporal
history live. The existing ground-contact guard takes precedence in either
scope. Supported cockpit-screen restoration remains aircraft dependent; Full
scene does not add restoration profiles for other aircraft. Resize/format
changes request a matching background context. One supported
scene/view, primary one-time command buffer, one graphics queue and existing
capture/depth checks are required. Unsupported paths bypass. The upstream
runtime currently requires an ASCII application path. Profile changes bypass
NR until ready; command buffers retain previous contexts until safe release.
NR + FG requires explicit experimental opt-in. The FG provider must be selected
before launch. The combination remains experimental and needs broader
live-flight quality and pacing validation. Disabling NR retains its context
for a quick live re-enable.

Fresh status contains PID, session token, timestamp and recording count. A
recording means commands were added, not independently proven scanout/quality.
Stale status is rejected and status/temp files reset on each launch. Import
and GPU checks cannot overlap launch, update or other checks. The launcher
waits for its NR task before closing.

## Build and validation

Build the app with `-FrameGenerationDevelopment -NeuralRenderingDevelopment`,
then run `Build-NR.ps1` with the compiler, glslang, Python, Vulkan headers and
pinned reference checkout. `-SkipShaders` requires matching recorded shader
hashes. `nr/NR-BUILD.json` lists only generated adapter/importer/check/shader
paths, sizes and hashes. Installer packaging requires its explicit NR switch,
validates every allow-listed path/hash and excludes models, caches and unknown
DLLs even when they exist beside the generated files.

Run `Test-NR.ps1`, `Test-Launcher.ps1`, `Test-LauncherUI.ps1`, updater regressions
and the GPU check.
For the larger color-format fixture:

```powershell
./app/nr/NRCheck.exe ./app/nr ./build/nr-check-720p --rgba16f-720p
```

On 2026-10-30 the real small network passed 256x144 RGBA32F and 1280x720 RGBA16F
fixtures on the R9700. The latest live-control fixture exercises 16 frames
(15 NR, one Off), dark/HDR content, artistic changes, history reset and two
passes: 58,982,400 finite samples, with the Off frame unchanged. The direct
sample/store optimization and conservative input-copy path produced identical
final float readbacks (SHA-256
`fb06ed1b1a28f9355073ceebf8a41593b906f3807b4036558232f335dd6b307e`).
Warm preparation was about 4–5 seconds; cold compilation can take tens of
seconds. These are preparation timings, not FPS measurements.

At 2560x1440 scene input, an A/B/A small-model benchmark measured median NR
pass GPU times of 3.13/2.14/3.36 ms at 50/33/50% model scale (30 timestamp
samples per run). The lower budget reduced this pass's cost about 32–36% in
that fixture. It does not establish simulator FPS gains or equivalent visual
quality. Lower model resolution may lose detail. Lowering blend strength alone
does not reduce network work.

The expanded live controls were exercised in X-Plane 12.4.4-r1 with FSR 4.1.2,
2560x1440 input / 3840x2160 output and the LevelUp 737. The FG-Off session
started with NR Off, enabled Subtle from the launcher, changed the model budget
50 -> 33%, and exercised Full scene cockpit processing, External-only cockpit
pause, live Off with a stationary recording count and resumed On processing.
It recorded 27,439 NR passes and exited normally. Live NR pass time was about
2.9–3.0 ms at 50%, about 2.0 ms at 33%; these are not total frame times.

A second session selected FSR 4.0.2 FG with explicit NR opt-in. Full-scene NR
recording, NR bypass with its count holding at 510 while FG kept advancing,
and re-enabled NR were verified without restarting that session. The session
recorded 2,075 NR passes and 2,883 generated frames and exited normally. Steady
source present-call intervals in one 512-sample window were approximately
32.8 ms median / 38.2 ms p95. Loading, UI and view transitions produced spikes
and protective bypasses. CPU present-call intervals are not scanout intervals
or a controlled performance comparison; sampled screenshots do not prove
absence of black frames, ghosting or pacing artifacts. Exact scene/object/cloud
motion and broader aircraft/driver quality remain unverified.

Some live scope/budget/On-Off changes used the development-only NRLiveCheck
probe after real launcher activation. The probe validates fresh PID, token and
canonical simulator executable before opening this project's existing control
mapping. It does not persist choices or inject code, and is not packaged.
UI persistence and scope selection are separately covered by native fixtures.

The NR selector, model-ready status and setup layout were inspected. Automated
regressions cover model identity/size rejection, regular build gating,
Vulkan aggregate/individual/core-1.4 chains, persistence, session cleanup and
existing updater flows. Main/NR scrolling regressions cover clipping, diagnostic
card containment, fixed footer, partial wheel deltas and repeated limit input
at 96/120/144/192 DPI. Live-flight visual validation and release/video review
remain separate gates; the synthetic check alone does not establish quality.

## Local assets, updates and removal

Models live under `nr/dlssnr-amd` once per application installation, not copied
into each simulator. Import preserves differing previous models with verified
backups. Updates preserve local models; generated uninstall instructions delete
only packaged files and leave user models/cache files. Returning to an older
Normal/FG release leaves NR inactive; older installers may leave unused NR
components in place. Remove local models yourself when no longer needed.
