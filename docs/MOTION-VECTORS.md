# Motion-vector research and 0.5.8 measurements

On branch `experimental/native-motion-vectors`, see the separate
[native renderer experiment](NATIVE-MOTION-EXPERIMENT.md). The research and
measurements below describe the regular 0.5.8 camera-derived implementation.

## Sources examined

- [JonathanOrr/xplane-fsr3](https://github.com/JonathanOrr/xplane-fsr3), MIT:
  reads scene SSR depth, aircraft flags and camera uniforms at the reflection
  pass. Its published Linux integration has no MSAA/VR/FG support and explicitly
  lacks per-object vectors. Its shader uses previous projection for zoom and
  separate world/own-aircraft reprojection. This is a useful future native-input
  backend, not a drop-in Windows module. It is not integrated in 0.5.8.
- [MotionVectors V2](https://github.com/Vihaan2012-cmyk/Motion-Vectors-for-Xplane-12-V2),
  GPL-3.0: patches vertex/fragment shaders, publishes camera transforms, and
  measures the field against image flow. Its README reports performance cost
  and remaining temporal instability. No code/binary from it is included here.
- [AMD FSR input requirements](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/):
  current-to-previous screen motion, jitter cancellation, geometry coverage,
  depth convention and reactive masks all matter. A generic optical-flow field
  is not equivalent to exact geometry motion or a reliable transparency mask.
- [Laminar plugin guidance](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/):
  graphics matrix datarefs are contextual to drawing and may be undefined in
  other callbacks. Drawing callbacks solely for bookkeeping add synchronization
  overhead. The present flight-loop metadata path therefore still needs live
  timing validation; no guarantee of renderer-synchronous transforms is made.

## Implemented changes

The current Windows depth-based path remains in place. The packing shader
selects world/body reprojection before applying it, computes eye coordinates
only for internal-view geometry and compares homogeneous squared distance with
no square root or perspective divide. Outputs remain R32 depth, RG16 motion,
R8 reactive data in one dispatch. Invalid depth/motion/eye transforms reject
history. MSAA depth selection and jitter convention are preserved.

The publisher caches the projection inverse until the matrix changes. It forms
inverse(P*V) as inverse(V)*inverse(P), reusing the view inverse for camera basis.
Affine view/body inversion uses double-precision cofactors, including scale and
float rotation rounding; it does not assume a perfect orthogonal rotation.
Nonaffine transforms retain the general inverse fallback. Unused sphere position
API reads are removed. ABI 4 and read-only controller/camera behavior are retained.
No new per-frame allocation, GPU readback, device wait or drawing callback is added.

## Evidence

On AMD Radeon AI PRO R9700, driver 26.10.1, five paired runs with alternating
baseline/new order, each timing 64 dispatches. Medians in milliseconds:

| Input | Samples | View | 0.5.7 | 0.5.8 |
|---|---:|---|---:|---:|
| 2560x1440 | 1 | External | 0.03567 | 0.03133 |
| 2560x1440 | 1 | Internal | 0.04449 | 0.03784 |
| 3840x2160 | 1 | External | 0.07444 | 0.06682 |
| 3840x2160 | 1 | Internal | 0.09241 | 0.08256 |
| 2560x1440 | 4 | External | 0.04539 | 0.04111 |
| 2560x1440 | 4 | Internal | 0.05413 | 0.04737 |
| 3840x2160 | 4 | External | 0.09992 | 0.09165 |
| 3840x2160 | 4 | Internal | 0.11195 | 0.10086 |

This is approximately 8-15% less packing time, not an overall FPS increase. The
synthetic inputs are cache-friendly and exclude depth preservation, FSR, FG and
simulator work. GPU clocks, contention and scene complexity can change timings.
The camera-algebra microbenchmark median fell from 262 to 68 ns per synthetic
calculation; it excludes XPLM calls and IPC and is not the full publisher cost.

The CPU test checks 1000 rounded/scaled large-world transforms, singular and
nonfinite input, projection reuse/zoom invalidation and equivalence to the prior
full inverse. Production-shader GPU tests compare vectors to independently
forward-projected 3D scene points for camera translation/rotation, zoom, body/world
selection, reverse/normal Z and viewport orientation. The old shader fails the
invalid-eye-matrix regression; the optimized shader rejects it safely.

## Remaining work

This release does not solve missing per-object, propeller, cloud or transparency
motion, determine the exact aircraft silhouette, or prove live matrix timing.
The four-metre cockpit classification is still a heuristic. SSR-native transforms
and aircraft flags could improve the source, but need a separate validated
capture backend with compatible resource lifetimes, viewport conventions, MSAA,
Windows SDK/FG and aircraft/weather testing. Importing unvalidated shader patches
would not establish correctness. Check live output in a controlled flight before
claiming universal ghosting-free behavior.
