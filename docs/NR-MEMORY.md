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
- Without an FG presenter, keep the 12 most recent submitted private frame
  caches per queue and retire colder completed one-time recordings. This bounds
  historical caches rather than accumulating full-resolution private images
  for every command buffer the simulator keeps. Outstanding work is never
  evicted. An FG session needs its presentation drain before shared output
  buffers can be reclaimed and is deliberately excluded from this cache cap.
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
  private frame image/NR scratch allocations. The image totals explicitly
  exclude SDK arenas, captured depth and simulator textures.

CPU tests exercise live owners, delayed destruction, non-overlapping profile
preparation, Off during construction, stale-result cancellation, rapid edits,
visible failures/retry and overflow-safe memory admission. A live comparison
must still measure memory before NR, after profile changes and after Off, and
confirm whether the same ground textures recover. These tests alone do not
establish a visual fix. One pass still costs less than three; model appearance
changes can soften details even with ample VRAM.
