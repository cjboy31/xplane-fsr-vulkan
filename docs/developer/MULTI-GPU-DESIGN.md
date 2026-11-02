# Multi-GPU processing design

**Stage 1 is in Test** (`src/dual_gpu.inc`, setting *Frame generation GPU*):
frame generation on a second GPU with the monitor on that GPU.

- X-Plane renders on GPU A as usual; its swapchain is replaced by a virtual one
  (images we own on A). Present copies the image to a host-visible buffer; FSR's
  depth and motion for the same frame are copied there from X-Plane's command
  buffer at the FSR boundary.
- A private device on GPU B is created through the system loader on X-Plane's
  instance (our own vkCreateDevice hook passes it through untouched), so it can
  present to X-Plane's window surface. Four queues are picked across B's
  families for AMD's FG swapchain; if that swapchain cannot be created, a plain
  swapchain on B shows the real frames.
- A worker thread waits for A's readback, copies host to host (baseline;
  zero-copy `VK_EXT_external_memory_host` import is next), uploads to B, records
  FSR FG PrepareV2 on B and presents through the FG swapchain.
- Three frames may be in flight; if B falls behind, frames go without generation
  rather than stalling X-Plane. Any set-up failure falls back to single-GPU FG
  for the session, with the reason in Diagnostics and the log.

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
