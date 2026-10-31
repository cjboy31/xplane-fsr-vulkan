# Multi-GPU processing design

Experimental branch `experimental/multi-gpu`, based on the released 0.7.2 source.
The installed release still has no multi-GPU offload. This branch now includes
bounded frame ownership, saved UUID selection policies, portable adapter/transport
probes and a real offscreen compute-only FG worker. It does not yet connect that
worker to X-Plane's live capture/presentation path or expose an active selector.

Lossless Scaling exposes a preferred processing GPU while the game keeps its
own rendering adapter. Its [publisher announcements](https://steamcommunity.com/app/993090/allnews/?l=english)
also describe queue/latency tradeoffs. This is a behavior reference, not source
code or permission to copy its interpolation algorithm.

## Settings to implement once the corresponding paths work

- Render GPU: display the adapter X-Plane actually created, including identity
  and dedicated-memory budget. Do not relabel a processing preference as a change
  to the simulator's existing Vulkan device.
- NR processing GPU and FG processing GPU: default to the render GPU. Display
  detected hardware and explicit reasons when a backend/device cannot run a task.
  Persist a device UUID rather than an enumeration index; disappearance or
  unsupported features must fall back with a visible explanation.
- Output/display adapter: distinguish it from the processing adapter. A
  cross-adapter present requires a real presentation path and display tests.
- Transfer/pacing diagnostics: real source FPS, generated/presented FPS, upload,
  inference, download, queue age, dropped/duplicate frames and memory budgets.
  Avoid a selector that appears active while the work still runs on another GPU.

## Transport constraints and first prototype

The current NR runtime uses X-Plane's Vulkan device and resources. Selecting a
different physical device requires a new device, its own allocations and an
explicit transfer/synchronization path. A Vulkan image handle from GPU A cannot
be passed to GPU B's command buffer. In particular, Khronos's
[external handle compatibility table](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryHandleTypeFlagBits.html)
requires matching device/driver UUIDs for opaque Win32 memory handles. The mere
presence of `VK_KHR_external_memory_win32` does not establish cross-card sharing.

Start with a bounded host-staging transport as a correctness baseline. Measure
two real adapters before considering supported peer/device-group or cross-API
paths. Query actual format/usage/handle support; never assume a simulator-owned
image was created exportable. Use correctly matched device IDs and fences or
timeline synchronization. The [Khronos device identity specification](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html)
defines UUID/LUID correlation.

NR before FSR needs a color round trip back to the render GPU before that same
frame's reconstruction. This is on the critical path; stale edits from another
camera frame must never be substituted. NR after FSR transfers a larger image.
FG offload additionally needs its guides, ordering and an independently valid
presentation integration. Keep the original real frame as fallback when a
matching completed result is unavailable, without presenting unwritten buffers.

Raw RGBA16F color alone costs about 63.3 MiB per 4K frame. At 60 source frames/s,
one direction is about 3.71 GiB/s, or 7.42 GiB/s for a round trip, before extra
guides, synchronization and bus overhead. At 1440p the round trip is about
3.30 GiB/s. These are byte-count calculations, not measured bus throughput.
Extra memory on a second GPU does not pool the simulator's VRAM.

## Validation gates

1. Enumerate adapters, stable IDs, heaps, queues and per-task feature support.
2. Verify transfer/readback correctness for both HDR formats, changing extents,
   camera cuts, missing/late results and device removal. Bound the staging ring.
3. Compare same-GPU and two-GPU transport/inference/present costs at 1440p/4K.
4. Prove work executes on the selected adapter, preserve settings and exercise
   automatic fallback. Test separate vendor and integrated/discrete cases.
5. Enable user-facing selectors only for paths with actual device/flight tests.
   Publish first as an experimental feature with measured limits.

## First implementation and evidence (2026-10-31)

- `gpu_frame_queue.hpp` bounds pending frames and refuses uncompleted/wrong-frame
  results. Capture, processing and consumption acknowledgements follow their GPU
  fences; reset cancels work without recycling an in-flight slot. CPU tests cover
  stale tokens, mismatched sizes/layouts, a full queue and concurrent cancellation.
- `gpu_selection.hpp` preserves NR/FG UUID choices and native fallback. A requested
  worker requires matching device/driver/session transport AND backend proof;
  buffer transfer or a same-physical fixture cannot certify offload.
- The layer's loader-visible physical-device lookup now uses unique device and
  driver UUIDs. Matching vendor/device/pipeline-cache IDs could choose the wrong
  member of an identical-card pair. Missing/ambiguous UUIDs fail safely.
- `MultiGpuTransportCheck` performs bounded fenced buffer round trips and a GPU
  marker on separate logical devices. `MultiGpuFrameGenCheck` additionally copies
  HDR images between independently owned devices, executes actual FSR 3.1.6
  interpolation using the SDK's NO_SWAPCHAIN_CONTEXT_NOTIFY mode, and copies the
  generated result back to the render device. No image handle crosses devices.
- The R9700 passed a **single-physical-card / two-logical-device** FG fixture:
  ten generated frames, seven checked non-black/finite HDR readbacks, a history
  reset and foreign/undersized-resource rejection at 384x216. Timings include
  host copies, fences and CPU pixel validation; they are not FG-only costs and
  predict neither 4K throughput nor simulator FPS.
- The Intel XeSS Vulkan offscreen adapter also passed cross-vendor readbacks on
  that R9700. See [Intel scope and pending gates](INTEL-VULKAN.md).

Only one physical Vulkan GPU is currently exposed on the test machine. Actual
cross-card transfers, Intel hardware, asynchronous scheduling, late-frame pacing,
UI composition and live X-Plane presentation remain unverified. Fixture depth and
motion are synthetic, not live scene capture. The synchronous
fixture is a correctness baseline, not the intended live implementation. The
simulator's original real frame must remain available until a matching generated
output and its return transfer have completed.

Next: a nonblocking bounded worker queue, measured two-physical-GPU transfers,
submission identity and memory accounting, then presentation/latency integration.
Do not enable a release selector until these gates pass. Existing Normal rendering,
its color-encoding fix and its completion/VRAM safeguards remain the baseline.
