# Sim performance and multithreading (exploration)

Status: research and plan, 2026-11-01. Nothing here ships until it is measured
on the owner's PC with the same flight, Off vs On, and passes the Test channel.

## What X-Plane already does (12.4)

Laminar's 12.4 work ([developer blog](https://developer.x-plane.com/2025/12/the-glorious-multi-core-future-is-now-the-boring-present/))
spreads scene-graph traversal (main and shadow passes), panel/avionics updates
and some draw preparation across worker threads. Two limits matter for us:

- **One command buffer.** The main thread still encodes all rendering into a
  single command buffer. Our Vulkan layer intercepts exactly that buffer, so
  every microsecond of CPU work our layer does there is added straight to the
  sim's frame time.
- **GPU-bound systems gain nothing** from more CPU threads, and VR is usually
  GPU-bound.

So the most useful "multithreading" for these mods is: keep our own work off
X-Plane's main thread, overlap our GPU work with X-Plane's, and let frame
generation carry displayed smoothness when the sim is CPU-limited.

## Where each idea helps

| Bottleneck | Idea | What it would change | Risk |
| --- | --- | --- | --- |
| CPU (main thread) | Move remaining mod CPU work to workers | Less time added to X-Plane's render thread per frame | Must keep strict frame ownership and safe bypass |
| CPU (main thread) | Pre-built descriptor sets / command fragments per swapchain image | Recording our passes becomes a few cheap calls | Invalidation on resize, MSAA or format change |
| CPU (stalls) | Pipeline/shader warm-up on a worker thread with a pipeline cache | Removes first-use hitches when a mode or profile is first used | Cache must be rejected on driver change |
| CPU (whole sim) | Opt-in **CPU scheduling helper**: keep X-Plane off efficiency cores (Intel hybrid) and on the cache-rich CCD (dual-CCD Ryzen X3D) via Windows CPU sets; disable power throttling for the process | Steadier main-thread time on those CPUs | Can hurt other apps; must be opt-in, reversible, per session, measured per CPU type |
| GPU | Run FSR / NR / FG work on an **async compute queue** | Overlaps our GPU passes with X-Plane's shadow/scene work | Queue ownership, semaphores; the SDK's async FG path is still fixture-only here |
| CPU-limited display | Frame generation + frame rate cap (shipped in v2 Beta) | Smoother displayed motion while the sim is CPU-bound | Does not make the simulation itself faster |
| Diagnosis | **CPU vs GPU frame-time panel line** | Tells users which side is limiting before they change settings | Must separate sim time, our layer time and waits |

## Order of work (pipeline priorities)

1. **P1 Measure first.** Add per-frame timers for the layer's own CPU work on
   X-Plane's render thread (capture, record, submit) and show them in
   Diagnostics next to X-Plane's frame time. This answers the owner's
   "CPU time much higher than GPU time" report and tells us which item below
   is worth doing.
2. **P2 Move our main-thread work to workers** where (1) shows it matters.
3. **P2 Opt-in CPU scheduling helper** (Windows): prototype with
   `SetProcessDefaultCpuSets` / `SetThreadSelectedCpuSets` and
   `SetProcessInformation(ProcessPowerThrottling)`. Off by default; restored
   at exit; compare 1% lows on the same flight for an Intel hybrid CPU and a
   dual-CCD X3D CPU. Single-CCD CPUs (for example a 9850X3D) are expected to
   see little change.
4. **P2 Pipeline warm-up** on a worker thread with a validated cache.
5. **P3 Async compute queue** for FSR/NR, then FG async workloads, after (1)
   shows a GPU-overlap opportunity.

## Validation for every item

Same saved flight, same weather and time, 2-minute fixed camera path. Record
average FPS, 1% and 0.1% lows, CPU frame time, GPU frame time and the layer's
own timers, each with the item Off and On, three runs each. Report the median.
A gain only counts when it is larger than the run-to-run spread.
