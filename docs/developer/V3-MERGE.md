# v3: one mod for FSR, Native AA and frame generation

Status: plan, 2026-11-01. Starts after v2 has passed Betas. Each step below
ships on its own through Test → Betas → Release Candidates, so users never
get a half-merged mod.

## Why merge

- One plugin, one panel, one installer, one updater, one settings file.
- No "choose a renderer and restart": the image processor becomes a setting.
- One Vulkan layer inside X-Plane's main-thread command buffer instead of two
  (see [SIM-PERFORMANCE](SIM-PERFORMANCE.md)).
- New combinations the separate mods cannot offer, chiefly **Native AA + frame
  generation** at full resolution.
- One motion-vector producer feeding every consumer (see
  [MOTION-VECTORS-PLAN](MOTION-VECTORS-PLAN.md)).

## What may run together

| Image processor | Frame generation | Neural rendering | Pacing + frame cap | Notes |
| --- | --- | --- | --- | --- |
| Native AA: SMAA / FXAA | Yes (new) | Yes (new) | Yes | Needs depth + motion for FG: shared producer |
| Native AA: TAA / TAA + finishing | Yes (new) | Yes (new) | Yes | Same jitter sequence for TAA and FG |
| FSR Native AA / Upscaling | Yes | Yes | Yes | As today |
| Off (original image) | No | No | n/a | X-Plane's own picture |

Never together: FSR with Native AA's TAA (two temporal AAs blur and trail),
or SMAA / FXAA before FSR (FSR needs the raw, jittered picture).

## Rules that carry over

1. **Native AA never needs AMD files.** Native AA modes must run with the FSR
   runtime absent (Intel, NVIDIA, Linux). FSR, FG and NR load AMD components
   only when chosen.
2. **Per-feature platform status.** Native AA keeps its native Linux beta;
   FSR on Linux stays labelled as the separate FSR 3.1 preview until parity.
3. **Saved choices survive.** First launch imports `NativeAA.ini` and the FSR
   `settings.ini`; nothing is reset. Both old installs are recognised and
   removed only when their files are unchanged.
4. **Truthful status.** The panel shows which processor actually completed GPU
   work this session, as both mods do today.

## Steps

| Step | Ships as | What changes | Done when |
| --- | --- | --- | --- |
| 1 Shared core | Both mods, still separate | One library for layer plumbing, panel-to-renderer link, camera publishing, panel drawing and hover help | Both mods build from it; all existing tests pass |
| 2 One plugin, one panel | v3 Beta 1 | Tabs: Image processing (Native AA / FSR / Off), Frame generation, Neural rendering, Diagnostics | Settings import from both mods verified |
| 3 One layer | v3 Beta 2 | One scene adapter with a processor switch; single installer and updater | Identity, isolation and lifecycle fixtures pass for every processor |
| 4 New combinations | v3 Beta 3 | Native AA + FG, Native AA + NR | Live flights: pacing, trails, cockpit text |
| 5 Retire old repos | v3 Release Candidate | Old repos point to the new one; old updaters get a one-time manual-install notice | Downloads and redirects verified |

## Repository

A new repository (for example `xplane12-image-suite`) holds v3. Both current
repositories keep their history, releases and tags. Final name to be chosen
by the owner.
