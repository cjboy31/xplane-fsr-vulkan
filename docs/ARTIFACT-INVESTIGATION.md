# Camera reconstruction correction in 0.6.3

The Windows Vulkan camera publisher now normalizes its projection to match
the native scene depth before deriving motion or distances. This corrects a
measured mismatch that produced large translation errors and visible edge
smearing. It adds no GPU pass, readback or optical-flow dependency.

## Diagnosis

In the tested X-Plane 12.4.4 session, `projection_matrix_3d` contained a forward
OpenGL perspective matrix, while the intercepted native Vulkan attachment used
reversed zero-to-one depth (clear 0, GEQUAL). Inverting that raw matrix and
treating native depth as its clip Z reconstructed the wrong scene geometry.
Changing only an FSR flag would leave world/body motion and distance-based masks
inconsistent.

A private diagnostic build sampled the native depth attachment on a flat apron.
The camera publisher reported 1.70 m terrain clearance. Independent unprojection
of ground samples produced:

| Mapping | Median reconstructed height |
| --- | ---: |
| Previous raw projection / native depth | 0.00549 m |
| Reversal without the OpenGL range conversion | 3.43575 m |
| Correct `z_gl = 1 - 2*z_vk` conversion | 1.71787 m |

The last mapping's median absolute height error was 0.01787 m. This distinguishes
the full range conversion from simply flipping the depth flag. The diagnostic
also switched legacy/corrected paths in the same running simulator: pausing hid
much of the problem, while motion exposed legacy smearing. These measurements
are local diagnostic evidence, not an all-hardware compatibility claim.

## Implementation

`camera_math.hpp` validates canonical perspective inputs and converts the clip-Z
row to `(rowW - rowZ) / 2` for forward OpenGL inputs. X/Y and asymmetric-frustum
terms are preserved. Already reversed Vulkan projections pass through unchanged;
nonfinite/unknown inputs are rejected. The publisher applies normalization before
its projection cache, inverse matrices, world/body reprojections and plane
extraction. FSR flags and shader reconstruction consequently describe the same
native depth. The shared-memory ABI remains unchanged.

The layer refuses a valid packet declaring the previous forward-depth convention
and leaves native rendering active with an update/restart explanation. It also
avoids injecting jitter/capturing temporal depth for that incompatible packet.
The existing launcher ownership checks back up and replace known publisher builds.
Unrecognized third-party files are refused.

## Validation completed

- 120 CPU world/depth/motion round trips cover finite/infinite and asymmetric
  projections, changing near planes, native pass-through and invalid inputs.
  Existing large-coordinate camera-basis tests also pass.
- GPU packing/reconstruction fixtures exercise 1x/2x/4x/8x MSAA, explicit and
  attachment resolves, observation, upscaling and Native AA.
- 24 camera-safety cases cover FSR 3.1.6 and 4.1.2, Native AA/upscaling, 1x/4x
  MSAA, stale packets, ground contact and the incompatible publisher, including
  fallback and temporal recovery.
- Live 1080p FSR 3 Native AA tests use acknowledged repeatable SDK camera yaw,
  lateral translation and ground-tilt sweeps. The corrected sequences show
  substantially cleaner edges than the reproduced legacy failure. The private
  camera-control fixture is absent from the product and was removed afterward.
- Live final-candidate FSR 4 Quality reports 2560x1440 input and 3840x2160 output
  on a Radeon AI PRO R9700, with the simulator's 2x MSAA option selected and FG
  Off. External yaw and a powered default cockpit were captured. Two cockpit
  displays were restored while inside. Native GPU MSAA packing is separately
  verified by fixtures; the selected live option alone is not proof of every
  simulator attachment's sample count.
- Captures retain packed RGBA FP16 HDR masters. Both comparison states receive
  the same measured white/peak normalization and SDR tone mapping. No sharpening,
  generated detail or image artifact repair is applied. Capturing at 15 fps is
  a review choice, not a simulator frame-rate measurement.
- Launcher update/owned-plugin backup, saved choices, media integrity and regular
  FG suspension tests pass. The simulator was shut down normally, all 51 original
  backed-up files hash-verified after restoration, and the private fixture removed.

Raw captures, diagnostic readbacks, simulator logs/preferences and SDK test tools
remain private and are excluded from source and release packages. The feature
guide uses stationary holds from the 1080p corrected-publisher capture; that
capture precedes the final incompatible-publisher guard, with a byte-identical
camera publisher. Final 4K motion captures exercise the completed guard build.

## Limits and regression checks

This does not supply complete native per-object/cloud animation vectors. Clouds,
propellers, moving objects, transparencies and unsupported cockpit displays can
still ghost. Tests cover one live GPU/aircraft and short controlled sweeps, not
all flights, drivers or add-ons. Linux and macOS preview packages are unchanged.
Built-in frame generation remains suspended in regular builds.

The ground-camera safeguard is deliberately retained: at external terrain
contact while on the ground, processing switches to native rendering at 1.5 m
or below and resumes above 2.5 m, inside the cockpit or after takeoff. An image
captured during that fallback is not evidence that FSR itself is artifact-free
at the floor. Camera collision/controller behavior is untouched by product code.

After updating, confirm recent **FSR ACTIVE** status, expected input/output sizes,
FG Off and the intended MSAA setting. Compare the same paused/then moving view
with processing Off/On. Exercise external near-ground contact, raise above 2.5 m,
pan clouds and runway edges, then check powered cockpit displays. Save the fresh
session log if residual trails remain; include aircraft, driver and exact mode.

## References

Laminar documents reversed Vulkan depth and DirectX-style clip control in its
[OpenGL drawing guidance](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/).
That guidance does not guarantee the observed projection encoding in every
simulator context; runtime validation and measured native attachment data are
the basis for this conversion. AMD documents the corresponding depth flags,
motion and projection requirements in its
[FSR upscaler integration manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/).
