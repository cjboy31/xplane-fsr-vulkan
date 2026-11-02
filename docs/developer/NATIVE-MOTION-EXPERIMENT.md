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
log gives the reason. Common reasons:

- MSAA is on
- frame generation is on
- the cockpit screen fix is on
- there is no matching SSR pass in this command buffer

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
3. **Use at the FSR boundary.** FSR, NR and the debug capture read the native
   images only if all of these hold: the capture belongs to this command
   buffer, this camera frame, this exact scene depth image and generation, and
   the same size. Otherwise FSR uses the camera-derived images. The camera
   packing pass still runs every frame. That costs one small dispatch, but it
   keeps the depth probe and the fallback ready.

Uniform-buffer descriptors are tracked only while native motion is selected. The
camera path's CPU work is unchanged.

## Limits (unchanged from the experiment)

- MSAA, frame generation, VR and the cockpit screen fix are off for native
  motion. That is stage D of the plan.
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
