# NR resource lifetime and texture memory

During the owner's ground-blur investigation, the image remained soft with NR
Off and with the mod's FSR processing Off. Live control status confirmed both
bypasses. Windows reported about 31 GiB of dedicated GPU memory attributed to
X-Plane, and the simulator logged heavy memory pressure. This supports a texture
paging investigation; it does not prove every blurry texture comes from NR or
memory pressure.

The fresh session exposed a larger cache contributor: 57–58 command buffers
retained about 8.4 GiB of private frame images, even after NR was bypassed. The
owner saw normal textures after selecting one pass, but live status showed a
memory-budget refusal at that point, not a confirmed successful one-pass NR
dispatch. That comparison must be repeated with NR genuinely active.

[Laminar explains](https://developer.x-plane.com/2022/09/x-plane-12-early-access-is-here/)
that X-Plane reduces texture resolution to fit available VRAM. Disabling image
processing alone cannot restore texture detail that the simulator has already
discarded. The texture pager may need time to load detail again after memory
is released. Input scale, scenery source textures and texture filtering also
remain relevant. The mod does not force private texture-pager controls.

## Changes

- NR Off, zero intensity and FSR-processing Off retire model sessions. Resources
  are destroyed on the background worker only after command-buffer owners have
  released them. Nothing frees resources still referenced by GPU work.
- Changing model resolution, pass capacity, processing order, network or image
  extent retires the old profile before preparing the new one. FSR continues
  without NR during this transition. Two large profiles no longer overlap just
  to make the live change. Rapid edits replace queued work; obsolete completed
  preparation is discarded rather than resurfacing after Off.
- Transition retirement uses an asynchronous queue fence to release completed
  one-time command recordings even when the simulator keeps their command
  buffers cached. A recording serial prevents an older fence from reclaiming
  resources used by a newer recording. Reusable or unsubmitted command buffers
  are retained. There is no steady per-frame CPU wait for GPU completion.
- Successful command-buffer rerecording drops NR scratch images when NR is Off,
  and drops the cached private frame resources when FSR processing is Off and
  no FG presenter was requested. An FG session retains shared output/depth/motion
  resources until its existing presentation drain; its work can outlive the
  application's source-frame fence.
- If a successfully rerecorded command buffer no longer uses private FSR work,
  discard its obsolete frame cache at End. Partial SDK failures and FG sessions
  are excluded. This handles command buffers reused for unrelated simulator work.
- Without an FG presenter, adapt the submitted private frame-cache allowance
  to image size and heap budget: target 1/16 of the heap budget, bounded to
  128–512 MiB, retaining 3–12 recent frames per queue. At a roughly 150 MiB 4K
  frame, this retains three eligible caches instead of twelve. It is a target,
  not a hard bound on in-flight work or all plugin memory. Retire colder completed one-time recordings. This bounds
  historical caches rather than accumulating full-resolution private images
  for every command buffer the simulator keeps. Outstanding work is never
  evicted. An FG session needs its presentation drain before shared output
  buffers can be reclaimed and is deliberately excluded from this cache cap.
- NR pauses at low runtime texture headroom, releasing its sessions without
  changing saved choices. It resumes only after the reserve plus the estimated
  model need fit again, providing hysteresis instead of repeatedly rebuilding
  at the threshold. The runtime gate is reported separately from the On switch.
- New private FSR frames and upscaler contexts have their own pre-allocation
  headroom check. Refusal retains the native simulator pass, retires unused
  caches and reports the reason. Existing resolution contexts were already
  bounded to two; this change does not claim their SDK memory is measured by
  the private-image counter.
- Preserved scene-depth images have an admission check using their actual
  allocation requirements. Failures before capture commands are recorded keep
  the native simulator work valid. Cold completed depth-capture caches are
  retired alongside frame caches, and diagnostics count their unique allocation
  bytes separately. NR also releases when the FSR renderer becomes unavailable.
- Before preparing a model, the adapter queries `VK_EXT_memory_budget` where
  available. A conservative size estimate plus texture reserve must fit the
  largest device-local heap's reported remaining budget. The reserve is 10% of
  the budget, bounded to 512 MiB–4 GiB. This is an estimate, not a guarantee that
  later scenery loads will fit. A missing driver budget is logged as unverified.
- Refusal leaves saved settings unchanged and reports the reason. A memory
  refusal is checked again after five seconds during eligible scene frames;
  lowering the work budget or toggling Off/On can retry sooner. No texture, resolution, AA or scenery
  preference is silently reduced. NR status distinguishes releasing resources
  from model resources released.
- Periodic renderer diagnostics report driver heap usage/budget and cached
  private frame image/NR scratch allocations plus unique preserved-depth images.
  These image totals explicitly exclude SDK arenas and simulator textures.

CPU tests exercise live owners, delayed destruction, non-overlapping profile
preparation, Off during construction, stale-result cancellation, rapid edits,
visible failures/retry and overflow-safe memory admission. A live comparison
must still measure memory before NR, after profile changes and after Off, and
confirm whether the same ground textures recover. These tests alone do not
establish a visual fix. One pass still costs less than three; model appearance
changes can soften details even with ample VRAM.

The mod can limit its own allocations and stop requesting expensive processing
under pressure. It cannot guarantee that the simulator, scenery, other plugins,
driver or other applications never exhaust memory. Report measured heap usage,
private cache size, active processing and texture quality together; a selected
NR option alone does not prove NR was applied.

The initial aggressive cache retirement exposed a CPU allocation-churn
regression. The private correction reuses compatible, legally completed images
inside additional bounded idle caches and reports their bytes separately.
See [CPU cost, ownership rules and validation limits](CPU-PERFORMANCE.md).
