# CPU cost and bounded GPU-resource reuse

## File operations removed from the frame path (development)

The 0.7.9 source still read processing/FG control files every 100 ms and wrote
FSR/NR status files every 500 ms on intercepted rendering calls. NR replacement
also requested a write-through flush. A slow or synchronized storage location
could therefore delay rendering while the device guard was held. This is a
code-path finding, not proof of the cause of a particular user's flight hitch.

The development bridge reads initial controls during device creation, then
polls them on a file-only worker. Rendering reads atomic cached values. FSR and
NR each offer one replaceable status snapshot using a non-waiting mailbox;
there is no accumulating frame queue. The worker owns copies of paths and
strings, never Vulkan resources, device pointers or renderer locks. Device
teardown joins it and drains accepted snapshots. Existing status identity,
stale-status checks, file replacement and NR durability remain in place.

Only a changed cached control is applied, so an old file value cannot repeatedly
undo a newer shared-memory On/Off switch. The existing live-switch and temporal
reset behavior is retained. Missing/invalid controls retain their last good
value. A busy mailbox skips an intermediate status and retries on a later frame.

Tests block the file worker inside a read and write while rendering-side calls
continue, exercise 10,000 coalesced snapshots, bounded payloads, I/O exceptions,
shutdown drain, Unicode paths, status identity and production live-switch
precedence. These checks establish removal of these particular file waits,
not an FPS increase. A fresh same-view flight comparison with NR/FG separated
is still required. Linux CI exercises the portable mailbox; this Windows-layer
change does not change Linux FSR rendering support.

## Observed regression

The memory-budget build reclaimed cold command-buffer caches but repeatedly
destroyed and recreated large images. Live logs showed roughly 1–2 GiB of
command-owned images retired in many quarter-second batches, even with NR and
FG Off. Depth identity changes also recreated the entire private frame set.
Those operations execute on the CPU/driver path while the device lock is held.

A stationary-view check on the R9700, X-Plane 12.4.4, 3840×2160 output and
2953×1661 input measured these pre-correction results:

| Live processing | Camera frames / elapsed wall time | Mean full-frame period |
| --- | --- | --- |
| Bypassed | 47.10 frames/s | 21.23 ms |
| FSR 4 only | 22.61 frames/s | 44.22 ms |

Each phase had five seconds of settling and a 20-second sample. Camera
direction, scene scope and output size stayed unchanged; NR and FG were Off.
The temporary IPC/file controls were restored and saved choices were untouched.
This is a sequential live comparison, not X-Plane's CPU/GPU-column measurement
or a fully uninstalled native baseline. It establishes substantial additional
processing cost in that scene, not that all CPU work belongs to the mod.

[Laminar's timing explanation](https://developer.x-plane.com/2025/12/a-very-quick-performance-primer/)
distinguishes CPU, GPU and total frame time. The
[Khronos memory guide](https://docs.vulkan.org/guide/latest/memory_allocation.html)
explains why frequent OS/driver allocation is expensive. Resource reuse here
retains compatible allocations; it is not a new general memory suballocator.

## Correction under private test

The first bounded-reuse build was measured in another stationary paired check:
47.84 frames/s (20.90 ms) bypassed and 34.09 frames/s (29.33 ms) with FSR only.
This improved the observed FSR-only rate from 22.61 frames/s, while the bypass
rate remained similar. It is not a claim about every aircraft/airport. Its CPU
counters still showed approximately 1,400–1,600 depth-image creations per
ten-second reporting window, with substantial depth setup and retirement cost.

- Move legally completed private frame sets and uniquely owned depth captures
  into bounded idle caches. A captured completion fence/recording serial, or a
  successful host reset/Begin for unused old resources, must establish safety.
  Pending resources are never admitted to an idle cache.
- Key reuse by device-local cache, command-pool queue family, dimensions,
  format and depth sample count. Discard old contents before recording fresh
  writes. Borrowed depth views/descriptors are rebound after completion instead
  of reallocating all private images, descriptors and uniform buffers.
- Limit idle frame allocations to 128–512 MiB (256 MiB with no driver budget),
  at most eight sets. Limit idle depth copies to 1/32 of the reported budget,
  bounded to 64–512 MiB (128 MiB when unknown), at most 24 images. These are
  separate idle limits; pending command work, other caches and SDK arenas are
  additional. Existing admission/texture-reserve checks remain in force.
- Clear idle resources on processing Off, renderer/allocation failure or memory
  pressure. Remove idle frames holding NR scratch after NR Off. Device teardown
  drains work and clears both caches before destroying the Vulkan device.
- Keep FG-presenter resources until both owners finish. See the follow-up below:
  FG being configured does not itself forbid reuse after SDK completion.
- Aggregate retirement logging instead of opening/flushing a file for every
  completed batch. New counters report intercepted CPU wall work, frame/depth
  setup cost and reuse hits/misses. These are not simulator CPU/GPU timings.
- A further refinement learns capture dimensions only from a current-camera
  depth/color relationship at the validated native FSR boundary. Skip depth
  copies and forced store operations at unrelated sizes; retain their metadata
  so a new valid input size can be learned. Keep two recent input extents and
  preserve native rendering while a changed input obtains a matching capture.
  Oversized inputs beyond the existing 4096-side contract are not captured.
  FG sessions retain their existing capture path.
- Reject null capture entries on allocation failure and candidate selection.
  A simulated Vulkan allocation failure checks that pre-recording depth capture
  bypasses safely, preserves the original image and leaves no null capture slot.

The existing GPU-memory line counts attached command images. Idle-cache bytes
are reported separately in the CPU/reuse line and must be added when estimating
owned image memory. SDK arenas and simulator textures are excluded. Retired
command-image bytes include resources moved to the idle caches; they do not
mean that all those bytes were freed from VRAM.

CPU tests cover pending-owner refusal, key mismatches, byte/count limits,
pressure and selective NR-scratch trimming, overflow and borrowed/shared
ownership. A GPU fixture executes production frame/depth creation, descriptor
rebinding and retirement code through 64 fenced image-clear/readback cycles,
checking contents and allocation reuse. It is not an FSR/NR quality fixture.
## Final private live sample

The capture-eligibility build measured 45.26 frames/s (22.10 ms) bypassed and
40.01 frames/s (25.00 ms) with FSR only in the stationary paired check. All 802
camera frames in the FSR sample had advancing FSR recordings. Camera direction,
scene scope and output size stayed unchanged, and temporary controls were
restored. NR and FG remained Off. These are sampled full-frame periods from
camera frame counts and wall time, not direct CPU/GPU-column measurements.

| Build | Live bypass | FSR 4 only | FSR-only mean frame period |
| --- | --- | --- | --- |
| Before correction | 47.10 frames/s | 22.61 frames/s | 44.22 ms |
| Completed-resource reuse | 47.84 frames/s | 34.09 frames/s | 29.33 ms |
| Scene-sized depth eligibility | 45.26 frames/s | 40.01 frames/s | 25.00 ms |

In representative ten-second FSR-only windows, the final build created 335–424
depth images versus roughly 1,400–1,600 before the extent refinement, and
reported 2,364–2,406 skipped unrelated-size depth passes. Depth setup accounted
for about 376–472 ms of CPU wall work per window; retirement accounted for
about 224–236 ms. Driver-reported heap usage was approximately 11–12 GiB in
those samples, with substantial budget headroom. These observations are not a
VRAM ceiling or a claim for NR/FG-enabled scenes.

The scene was reloaded between builds, so this is a local development comparison
with a paired bypass control, not a universal FPS guarantee or a fully
uninstalled native baseline. GPU allocation/readback tests and these stationary
measurements do not certify moving-camera, all-aircraft or NR/FG visual quality.

## Configured-FG allocation regression

A later 4K-output flight still stuttered with NR and live FG Off. FG had been
configured at startup. The blanket FG cache exclusion was still active, and a
changed borrowed-depth view forced a full private-frame rebuild. In representative
ten-second windows this created 150–179 frame sets and 514–577 depth copies.
Frame setup took 774–916 ms, depth setup 580–666 ms and retirement 455–539 ms
per window. These are intercepted CPU wall costs, not scanout or X-Plane's
CPU/GPU columns. Source-present p95 remained about 35–39 ms with live FG Off.

Compatible frame/depth allocations can now be reused when both the application
recording and the presenter's monotonic lease have completed. Changing only the
borrowed-depth view updates that completed frame's view/descriptor instead of
allocating all its images. An SDK-owned old recording is moved to a separate
owner list after a legal application Begin/free, so the next recording cannot
overwrite its images. It enters the bounded idle cache only after SDK completion.
Explicit idle/context-drain operations also publish the completion they proved;
they do not advance beyond the lease captured before the wait.

There is no new per-frame wait. Existing memory admission, texture reserve,
idle byte/count bounds, source matching and FG synchronization remain in place.
The owner list is not an idle cache: pending SDK work cannot be freed to meet
an idle limit. Its image bytes are reported separately. The existing presenter
memory-pressure drain still governs the asynchronous path.

The production GPU fixture now checks 128 fenced readbacks with FG absent and
configured, including reusing actual 2953×1661 input / 3840×2160 output
allocations. Each run creates one frame set and one depth allocation, reusing
both 127 times. It also calls the production Begin/End hooks while the SDK
lease is pending, checks retention through live Off, then cache admission after
completion. These are ownership/content checks, not a measured simulator FPS
improvement. Application fences and GPU readbacks are real; presenter leases
in the resource fixture are simulated. The separate SDK test also passed both
FG providers in UNORM/sRGB, generated-frame readbacks and the updated layer's
swapchain create/acquire/present/recreate/idle/destruction lifecycle. The full
Windows CPU/settings/startup/updater/panel regression suite passed. Fresh
moving-flight and FG-on pacing validation remains required.

References: [Vulkan command-buffer lifetime](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html)
and [AMD's frame-generation API ownership guidance](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-api/).
