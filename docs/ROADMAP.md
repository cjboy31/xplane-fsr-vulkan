# Community roadmap

## Next implementation priorities

1. Make an original LuminaXP-style in-simulator setup the primary interface:
   an accessible Plugins menu panel, presets/graphics, live NR controls,
   models, diagnostics, guides/updates, persistent settings and early activation
   for direct and third-party launches. Keep the desktop application on GitHub
   as a clearly labeled **optional secondary interface**, sharing saved choices.
   No special desktop launch step should be required after installation. See
   [the integration design and required checks](IN-SIM-PLUGIN.md).
2. After the current NR memory/blur corrections are tested (the owner's latest
   priority), begin multi-GPU operation alongside the remaining panel work, using Lossless Scaling's
   documented behavior as a reference. Evaluate separate render/processing GPU
   selection, cross-device transfer costs, synchronization and fallback. Require
   actual two-GPU validation before advertising compatibility or speed gains.
   See [the transport design](MULTI-GPU-DESIGN.md). Start with a real adapter/
   feature inventory and measured transfer prototype before enabling a selector.
3. After in-sim integration, investigate the owner's higher CPU frame time.
   Compare one reproducible scene with the mod bypassed, FSR only, NR and FG;
   separate simulator CPU work, driver/queue waits and source/display timing.
   Include the [Agility SDK research findings](AGILITY-SDK-REVIEW.md): prioritize
   measured Vulkan batching, allocation/compilation and NR scratch-memory costs
   before considering a separate Direct3D compute-graph backend.

The Windows 0.7.2 release includes the in-sim preview and measured CPU/memory
corrections. Full panel parity remains in progress. The new multi-GPU/Intel
worker fixtures are on `experimental/multi-gpu`; they are not installed features.
Implement independently without copying restricted reference-mod internals.

4. Bring Intel graphics into the backend pipeline: use the official XeSS Vulkan
   interface for SR, then assess FSR 3 FG on Intel and the separate DirectX 12 /
   XeLL requirements for native XeSS FG. Require real Intel GPU and simulator
   tests; see [implementation](INTEL-VULKAN.md).
5. Add Linux FSR 4 AA/upscaling and NR independently of FG. Run portable preflight
   CI, identify a supported native FSR 4 runtime, and port the NR adapter/IPC onto
   the pinned Linux Vulkan core. See [pipeline and dependency gates](LINUX-FSR4-NR-PIPELINE.md)
   and [tracking issue #3](https://github.com/cjboy31/xplane-fsr-vulkan/issues/3).
   These tracks add no new Linux download or release date yet.

## Proposed improvements and how to evaluate them

| Idea | Intended benefit | Evidence required before shipping |
| --- | --- | --- |
| NR resource release and texture reserve | Reduce avoidable pressure on scenery textures | Compare the same flight's heap budget, owned allocations and ground detail across NR Off/1/3 and profile changes. Current private implementation is under live test. |
| NR/FG processing GPU selection | Move supported post-processing work off the render GPU | Verify feature support, transfer bandwidth, GPU ownership, end-to-end latency and two physical GPUs; compare with the same-GPU path. VRAM is not pooled. |
| Warm shader preparation with a cancel/progress indicator | Reduce first-use stalls and make preparation understandable | Measure cold/warm compilation on each supported driver, reject stale caches and avoid compiling large unused profiles. |
| CPU/queue timing diagnostics | Explain high CPU frame time and identify actual blocking work | Separate simulator work, capture/dispatch, queue locks, compilation and present waits; compare median and tail frame times in one scene. |
| Optional adaptive NR budget | Keep a user-selected frame-time target during demanding views | Use measured GPU cost and hysteresis; display the effective budget and retain the user's maximum. Test visual transitions and avoid oscillation. Proposed only. |
| Lighting/color-only NR composition | Retain source texture and readable cockpit details | Use matched HDR source/output captures and quantify retained detail, halos, moving cloud quality and text. Do not add sharpening as a substitute for missing source textures. |
| Better cloud/transparent/object rejection | Reduce temporal trails around clouds, propellers and moving aircraft | Obtain reliable guides or confidence masks and test occlusion/motion; camera-only vectors cannot describe every moving object. |
| Clean UI composition after FG | Keep simulator menus and avionics stable | Verify per-frame UI/scene separation, color space, present count and resume after opening/closing panels. |
| Repeatable comparison/diagnostic capture | Make regressions and community reports reproducible | Match view/time/weather/exposure, mark real versus generated frames and preserve or correctly tone-map HDR. No user logs uploaded automatically. |
| Native Metal backend research | Establish a real Mac rendering path | Identify scene/depth/motion/present integration, implement native IPC and test on Mac hardware. Existing Mac diagnostics do not provide rendering. |

These are implementation candidates, not promised gains. Prioritize correctness,
memory headroom and measured CPU/queue bottlenecks before adding expensive effects.

## Ongoing rendering work

- Verify default SR22 PFD/MFD alignment, depth tolerances and matrix timing in
  stationary, taxi, flight, head movement and resized/FOV-changed views.
- Add a deterministic real-render boundary fixture for the compositor integration.
- Capture clean native avionics textures and UV mappings to restore full output
  detail instead of replaying reduced-resolution scene samples.
- Add proper after-FG UI composition and robust generated-frame occlusion.
- Improve moving-object, propeller, cloud and transparency motion/masks.
- Measure GPU pass cost and source-frame pacing against identical native flights.
- Extend profiles using original parsers/metadata, never embedded aircraft assets.
- Resolve outstanding Vulkan validation problems, especially FG lifecycle/errors.
- Expand aircraft, driver and GPU coverage only after reproducible evidence.
