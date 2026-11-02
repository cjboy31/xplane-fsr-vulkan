# Motion vectors: priority plan (v2.x → v3)

Status: **P1 priority** set by the owner, 2026-11-01. This plan is shared by the
FSR / frame generation mod and Native AA. Background research and the 0.5.8
measurements are in [MOTION-VECTORS](MOTION-VECTORS.md).

## Why this is the top rendering item

X-Plane renders no velocity buffer. Today both mods rebuild motion from depth
and camera matrices. That is right for terrain, scenery and the sky. It is wrong
for anything that moves on its own: other traffic, propellers, control surfaces,
gear, and the own aircraft when only parts of it move. Wrong motion is what
causes most of the remaining trails and smearing, in three places:

- **FSR upscaling / Native AA:** wrong vectors leave ghosting behind moving
  objects.
- **Native AA TAA:** history is rejected too often, so moving objects shimmer.
- **Frame generation:** generated frames warp moving objects most visibly.

Better vectors improve all three at once, so this item ranks above new AA modes.

## Rules

- **Original code only.** *Motion Vectors for X-Plane 12 V2* is GPL-3.0. We may
  read its README and published measurements but never copy its code or shaders.
  The SPIR-V classifier adapted from MIT
  [JonathanOrr/xplane-fsr3](https://github.com/JonathanOrr/xplane-fsr3) keeps its
  license file and pinned commit.
- **No simulator files are changed on disk.** Any shader work happens in
  memory, in the layer, with a strict interface check. If the check fails, the
  layer falls back to camera-derived motion and the status line says so.
- **Native AA stays free of the AMD runtime.** The producer is plain Vulkan
  compute.
- **Each stage ships on its own** through Test → Betas → Release Candidates,
  with camera-derived motion still selectable.

## Stages

| Stage | P | What | Done when |
| --- | --- | --- | --- |
| A Shared motion contract | P1 | One format for motion, depth and reactive data, plus one producer that FSR, FG and Native AA TAA all read (same jitter, same sign and units, same reset rules). Today each mod has its own path | Both mods use the contract; Off/On images match the existing paths bit for bit in fixtures |
| B Native SSR capture, revived | P1 | Rebase `experimental/native-motion-vectors` (0.6.0-experimental.1) onto v2 `test`. It reads X-Plane's SSR pass inputs: depth, own-aircraft mask, world and aircraft reprojection, previous projection. Offer it as **Motion source: Native (experimental)** | Classifier rejects unknown interfaces; it bypasses cleanly; live flight shows less trailing on the own aircraft when zooming and turning |
| C Measure vector quality | P1 | Original tool: warp the previous frame with our vectors and compare it to the current frame (error per pixel, split by sky / terrain / aircraft / cockpit). Runs on fixture captures and on `tools/render_evidence.py` output | Each stage gets a number, not just "looks better" |
| D MSAA and FG with native motion | P2 | Remove stage B's limits: MSAA resolve of the motion data, frame generation fed native vectors | FG fixtures pass with native motion; no new device losses |
| E Moving objects | P2 | Per-object motion for traffic, propellers, control surfaces and gear, via in-memory instrumentation of the draw that writes previous-frame positions, plus reactive masks for clouds, particles and transparent glass | Stage C error falls for traffic and propellers on the same flight |
| F One producer in v3 | P2 | The merged mod (see [V3-MERGE](V3-MERGE.md)) runs the producer once per frame for every processor | One dispatch per frame no matter how many consumers |

## Risks to watch

- X-Plane updates can change the SSR interface. The classifier must fail
  closed, and every X-Plane beta gets a fixture rerun.
- Instrumenting draws (stage E) costs CPU on X-Plane's main thread. Measure it
  with the per-frame layer timers from [SIM-PERFORMANCE](SIM-PERFORMANCE.md)
  before shipping.
- Zink on and off must both be tested; Zink changes which GL work reaches
  Vulkan.
