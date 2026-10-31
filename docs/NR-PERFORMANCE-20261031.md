# NR cost and flight-pacing investigation

## Measured NR work

The R9700 development system ran the existing driver-compiled NR backend with
the locally supplied Small model, one pass and linear RGBA16F input. Each
setting completed 40 fenced synthetic frames; the first ten were excluded from
GPU timing. The following values are medians from 30 delayed, non-waiting Vulkan
timestamp samples. They include NR processing, not simulator CPU work, FSR or
frame generation.

| Input | Model size 33% | Model size 50% | Model size 67% |
| --- | ---: | ---: | ---: |
| 2560 x 1440 | 2.07 ms | 3.06 ms | 4.36 ms |
| 3840 x 2160 | 3.16 ms | 5.70 ms | 10.15 ms |

At 4K, choosing Balanced 50% instead of Quality 67% reduced measured NR work by
about 44%. This is a setting tradeoff, not a new faster neural network or a
44% improvement in flight FPS. Lower model sizes can change fine detail. The
benchmarks check finite output and actual model dispatch; they do not establish
matched flight quality, moving trails or cloud clarity.

Before upscaling saves work only when the simulator supplies a smaller input.
With Native AA, the input is already full size. Lowering effect strength blends
the result but does not avoid network evaluation. The useful first comparison
is Small/Auto, one pass, Balanced 50%, before upscaling, with FG Off. Preserve
the user's choices; do not silently lower a saved Quality profile.

The existing backend already uses delayed GPU timestamps without WAIT, in-place
compatible color input, cached driver pipelines, background profile preparation,
and one-pass allocation capacity when one pass is selected. Cold shader pipeline
compilation took roughly 43-45 seconds for newly encountered benchmark sizes;
one subsequently cached profile prepared in about half a second. These are
offline observations. Compilation and live profile switching need separate
flight checks before attributing sustained low FPS to setup work.

The primary pinned source is
[DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD/tree/960038fd1cb892742548f408355e0f3b2fd30d6f).
The integration keeps its reviewed SPIR-V route. Unverified precompiled machine
code and model files are not imported into this release. The
[Khronos pipeline cache guidance](https://docs.vulkan.org/guide/latest/pipeline_cache.html)
describes reusing driver pipeline state across runs.

## Separate pacing candidates under test

The last user session also showed present-interval spikes after NR was switched
Off. That observation was not a controlled stationary comparison, so it does
not assign blame to the simulator, FG or a particular plugin.

- Repeated temporal resets previously wrote a diagnostic line for every frame.
  Detail is now limited to startup, periodic summaries and at most one reset
  detail every two seconds. Counts retain all resets and omitted detail. Error,
  activity and session logging are retained. Rendering decisions are unchanged.
- Scene-sized depth capture previously learned dimensions only with FG Off.
  The test branch also learns the validated reconstruction input during FG
  sessions and avoids forcing stores/copies at unrelated sizes. It retains
  broad capture until a valid input is known. A changed input can retain native
  rendering briefly while its new size is learned. Presenter lifetimes, GPU
  completion checks, FG cache retirement and the exclusion of FG resources from
  the ordinary reuse cache are unchanged.
- Reset detail now identifies camera, missing-ready-path, depth identity,
  depth generation, NR control and NR application transitions. This reports
  causes without weakening temporal safety checks.

CPU/native UI regression and the 64-fenced-readback ownership/OOM fixture pass.
These candidates still need the same-view NR On/Off and moving-flight tests.
Do not advertise a completed stutter or NR quality fix from these fixtures.

## Provider names

SDK 2.4.0 is a bundle version. Its selected upscalers are 3.1.6 and 4.1.2; its
selected frame-generation algorithms are 3.1.6 and 4.0.2. Presentation providers
have separate versions. In-sim labels use the algorithm versions rather than
calling frame generation 4.1. See [AMD's SDK component list](https://gpuopen.com/amd-fsr-sdk/).
