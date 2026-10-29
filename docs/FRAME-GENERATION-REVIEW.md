# Frame generation: suspension and return criteria

Review date: October 29, 2026. Starting with 0.6.1-alpha, regular releases disable
the built-in frame-generation path. The existing experimental branch/release
retains the development implementation. This is a quality precaution based on
the repeated user reports, including the report that flickering stopped with
FG Off. It does not establish the cause of every artifact or stutter.

## Current integration

The implementation replaces Vulkan presentation with AMD's SDK swapchain and
prepares depth/motion inputs at the scene-upscaling boundary. It already checks
submitted source matching, supports temporal resets and has a ground-camera
safeguard. The regular motion path derives motion from camera/depth and a coarse
own-aircraft transform. It does not provide exact independent motion for clouds,
traffic, props, gauges or other effects. The native-motion experiment also has
explicit input and coverage limits.

These limitations matter to interpolation. AMD's API requires render-resolution
motion vectors and depth in supported formats; its own optical flow does not
make inaccurate engine-side inputs equivalent to complete motion. Passing
small GPU fixtures and reporting extra presents do not establish clean flight
imagery, good latency or correct display pacing. See
[AMD's FG integration overview](https://gpuopen.com/amd-fsr-framegeneration/).

## What Lossless Scaling teaches us

Lossless Scaling describes its scaling/FG technology as proprietary. Its public
descriptions are useful architectural evidence; they do not supply source or
permission to redistribute its runtime/model. Nothing from that product is
bundled in this mod. See the [official product site](https://losslessscaling.com/).

The developer's public updates distinguish unbuffered capture from bounded
queues that trade latency for capture stability. They also discuss UI detection,
flow resolution, low-base-FPS behavior and capture API changes. Its frame-pacing
notes describe queue latency and presentation synchronization as deliberate
choices. These suggest independent work on freshness, bounded buffering, UI
exclusion and pacing—not that copying a queue count would reproduce LSFG quality.
Sources: [official announcements](https://steamcommunity.com/app/993090/announcements/),
[official frame-pacing notes](https://losslessscaling.com/framepacing-and-lsfg-updates/).

## Required work before public return

1. Identify a reliable color boundary and keep cockpit instruments, menus and
   other UI out of interpolation or compose them separately after generation.
   Verify exact frame/extent/encoding provenance at every boundary.
2. Compare the current motion with an independently licensed image-motion
   estimator or a complete engine source. Measure behavior at disocclusions,
   cloud/terrain edges, rotating props, traffic and camera cuts. Label estimates
   accurately; another project's success is not this integration's validation.
3. Bound pending work and prefer fresh, matched frames. Define reset/bypass
   behavior for long frame intervals, repeated sources, menu/loading transitions,
   GPU saturation, resize and missing data. Do not hide poor base frame times
   by reporting a higher generated-frame count.
4. Measure actual presented timing, visible duplication/drop patterns and input
   latency at fixed refresh and VRR. CPU present-call intervals alone are not
   scanout timing. Test multiple driver/GPU combinations and long flights.
5. Validate the reported external ground camera, cockpit displays, clouds and
   moving scenery in real flight recordings and independent user tests. Compare
   FG Off at the same base settings and retain an immediate reliable bypass.

Any replacement must undergo source/license review, synchronization and failure
tests, then a separately labeled experimental release before regular enablement.
No new frame-generation algorithm or LSFG-equivalent quality is claimed here.
