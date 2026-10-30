# Async frame-generation investigation

The application still defaults to synchronized FG. Async is available in the
isolated `FrameGenCheck` developer fixture, not as an advertised in-flight mode.
Both paths use the same missing/duplicate-source hold and reset recovery.

[AMD's integration guidance](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
requires `FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT` at context creation
and `allowAsyncWorkloads=true` when configuring it. A different queue alone does
not enable async compute. Disabling async still leaves paced presentation/UI on
other queues. CPU calls touching a context must remain serialized either way.
Any external HUD-less/UI images must retain their documented lifetimes; this
experiment provides no such external images. The real backbuffer remains enabled.

`FrameGenerator::create(..., asyncWorkloads=false)` preserves the default for
every application call site. `FrameGenCheck` accepts `--async` explicitly and
creates the required capability before selecting that schedule. Its queue set
contains four distinct reserved graphics-family queues. The normal readback
fixture waits for completion to inspect images. The `--benchmark` variant
removes each frame's presentation/device-idle waits, disables readback and waits
only for its reusable graphics command buffer/final completion, allowing the
SDK queues to overlap. It retains FIFO presentation and the same CPU upload and
8 ms source delay. It is not a simulation of X-Plane's full GPU workload.

Run with X-Plane closed, using the matching newly built app:

```powershell
./Test-FGAsync.ps1 -AppDirectory ./app
./Test-FGAsync.ps1 -AppDirectory ./app -BenchmarkOnly
```

The first command checks synchronous/async schedules and injected missing-source
gaps on both providers. The second measures sync/async/sync for each provider.
Evidence stays under ignored `build/`. No results or private GPU data are
automatically uploaded. `Test-FGFallback.ps1` additionally checks matching
creation/configuration flags and that both paths preserve error protection.

## R9700 controlled results, 2026-10-30

On driver 26.10.1, each async provider (3.1.6 and 4.0.2, 4.0.0 presenter) passed
90 real frames with three injected missing-source holds, 87 generated-frame
readbacks without black regions, and 177 SDK presents. Startup/reset outputs
were included. Synchronous runs passed the same checks.

| Provider | Sync A sources/s | Async sources/s | Sync B sources/s |
| --- | ---: | ---: | ---: |
| 3.1.6 | 64.63 | 64.39 | 64.63 |
| 4.0.2 | 64.53 | 64.43 | 64.41 |

Each timing run used 240 source frames and three injected gaps, with 477 SDK
presents. This small FIFO/upload workload showed no meaningful async gain.
These values are synthetic source throughput, not X-Plane FPS, display scanout
times, a latency measurement or proof of live-flight quality. A useful next
gate is GPU-busy X-Plane A/B/A profiling with NR On/Off, retained resource
lifetimes, source/scanout pacing, and resize/cut/shutdown stress. Until then,
switching the application default or promising a speed-up is unsupported.
