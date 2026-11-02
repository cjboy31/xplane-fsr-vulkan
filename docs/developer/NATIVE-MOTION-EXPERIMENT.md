# Native renderer motion (experimental, v2)

Status: in the private **Test** channel since 2026-11-01. It is P1 stage B of
the [motion vectors plan](MOTION-VECTORS-PLAN.md). The 0.6.0 experiment came
from branch `experimental/native-motion-vectors`. This version runs inside the
v2 build and is no longer a separate install.

## What the user sees

The in-sim panel's **Image processing** section has a new row, **Motion
source**:

- **Camera** (default): the existing camera-derived motion.
- **Native (experimental)**: uses X-Plane's own reflection-pass inputs.

The choice is saved as `MotionSource` (0/1) in `settings.ini`. The session
profile passes it to the layer as `XPFSR24_MOTION=native-renderer`, and only for
an FSR session. It applies after a restart.

When native motion cannot run on a frame, FSR keeps working with camera-derived
motion. The status line says **Camera-derived motion used**, and the session
log gives the reason. The reasons are:

- X-Plane's SSR pass is in a reused or secondary command buffer
- the SSR descriptors were not tracked
- the SSR inputs do not match this frame's scene depth
- there is no matching SSR pass in this command buffer

Since stage D, native motion also runs with MSAA, frame generation and the
cockpit screen fix.

Switching source resets history once (reset bit 64 in the `reset-sources` log
field). That follows the shared motion contract's continuity rule.

## How it works

1. **Recognise the shader.** When X-Plane creates its scene SSR compute shader,
   a strict SPIR-V classifier checks the shader's interface. The classifier is
   `src/native_motion/shader_classify.*`, adapted from MIT
   [JonathanOrr/xplane-fsr3](https://github.com/JonathanOrr/xplane-fsr3) at
   `fa51577c9bc2c3477e64dfdd358796aebb9ca45b`; its licence is in
   `licenses/xplane-fsr3-MIT.txt`. It checks:
   - names
   - descriptor sets and bindings
   - member offsets
   - matrix layout and strides
   - scalar types

   Any change to that interface makes it return `None`, so the layer fails
   closed. Classification runs only when native motion is selected, and outside
   the device lock.
2. **Capture after X-Plane's SSR dispatch.** The layer runs one extra compute
   dispatch, `src/native_motion.comp`. It reads the scene depth, the
   own-aircraft stencil flag, the world and aircraft-local reprojection
   matrices, and the previous projection, which is kept on the GPU. From those
   it writes depth, motion and reactive data in the shared motion contract v1
   formats. A one-thread pass then updates the projection history. After that,
   X-Plane's compute state is restored.
3. **Use at the FSR boundary.** The native result is used only if all of these
   hold: the capture belongs to this command buffer, this camera frame, this
   exact scene depth image and generation, and the same size. When it is used,
   the native images are copied into the frame's own depth, motion and reactive
   images, and the camera packing pass is skipped. Every consumer then reads one
   set of images with its existing lifetime protection: FSR, NR, frame
   generation (whose presenter can outlive the command buffer), the cockpit
   screen fix, the depth probe and the capture. When the native result is not
   used, the camera packing pass runs as before.
4. **MSAA (stage D).** The classifier now applies the full layout checks to
   X-Plane's MSAA SSR shader too (2D only; arrayed MSAA is rejected). A
   multisample shader variant takes the nearest covered sample (reversed Z),
   together with that sample's aircraft flag. This is the same rule as the
   camera path's MSAA depth resolve.

Uniform-buffer descriptors are tracked only while native motion is selected. The
camera path's CPU work is unchanged.

## Limits (unchanged from the experiment)

- VR is not covered. MSAA, frame generation and the cockpit screen fix were
  enabled in stage D. They have not been tested live yet; fixture coverage for
  MSAA is classification only.
- Capture needs one-time primary command buffers on the graphics queue. The SSR
  pass must be in the same buffer as the FSR boundary.
- The aircraft flag covers the own aircraft as one rigid body. Traffic,
  propellers, control surfaces, clouds, rain and glass still have no exact
  vectors. That is stage E.
- There is no live-flight validation yet. Fixture checks do not establish image
  quality.

## Tests

| Check | Where | What it proves |
| --- | --- | --- |
| `src/native_motion/classify_tests.cpp` | Linux CI | Stand-in SSR interfaces (2D, array) are recognised. Every changed uniform offset, a truncated module and a bad magic number are rejected. |
| `Test-NativeMotion.ps1` | Windows GPU | Capture, reconstruction, projection history and state restore with original stand-in shaders. |
| `Test-NativeMotion.ps1 -Integration` | Windows GPU, after `Test.ps1` | Loader + FSR 3.1.6 / 4.1.2 with `native-2d`, `native-array` and `native-missing`. The missing case must log the camera-derived fallback and never claim native motion. |
| `src/activation_tests.cpp` | Windows CI | `XPFSR24_MOTION` is set only for FSR sessions with Native selected. |
| `src/in_sim_panel_tests.cpp`, `settings_transaction_tests.cpp` | Windows CI | The setting is saved, clamped and merged without overwriting other editors. |

## Measuring it: motion-quality capture

Measure before claiming it is better.

1. Close X-Plane.
2. Set a user environment variable `XPFSR24_MOTION_CAPTURE` to an empty folder.
3. Optional settings:
   - `XPFSR24_MOTION_CAPTURE_AFTER`: recorded frames to wait first. The default
     is 900, about 15 seconds.
   - `XPFSR24_MOTION_CAPTURE_CROP`: the centred crop size. The default is 1024;
     the maximum is 2048.
4. Fly the same saved flight. Pan or turn steadily while the capture is taken.

The layer writes `motion-capture-<pid>-<ticks>/` with `capture.json`,
`frame0.bin` and `frame1.bin`. These hold two consecutive frames of FSR's exact
inputs, from one source with no reset between them. Then run:

```
python tools/motion_quality.py <folder> --json report.json --heatmap error.png
```

The report gives a **gain** per region:

- sky
- aircraft (pixels the producer marked)
- near scenery (< 100 m)
- far scenery

Gain is how much of the frame-to-frame difference the vectors remove compared
with no motion. Capture once with **Camera** and once with **Native**, using
the same flight and camera move. Compare aircraft and near gains. Clear the
variable afterwards. The capture is a one-time tester tool and is never on by
default.
