# Native renderer motion: separate experimental option

Branch: `experimental/native-motion-vectors`.
Version: `0.5.9-experimental.2`. The regular `0.5.9-alpha` release remains available.

This update shares the regular release's per-process log reset, immutable
camera-frame checks and ground-camera safeguard. With the aircraft on the
ground, an external camera within 1.5 m of terrain temporarily uses native
rendering; the app shows GROUND CAMERA SAFETY. Raise it to at least 2.5 m, enter
the cockpit or take off to resume with fresh history. Both motion choices obey
this guard. It does not move the camera or change saved selections. Native
rendering can change AA and frame rate. The original report still needs a live
retest. See [diagnostics and retest steps](RENDERING-STABILITY.md).

## Choose which implementation to test

The main screen now has a **Motion** selector. **Camera-derived** is still the
default and keeps the existing features. **Native renderer motion — EXPERIMENTAL**
is a second, explicitly selected implementation. The choice is saved, including
when graphics are edited or a preset is applied. Restart X-Plane to change the
active motion source.

For the native option, close X-Plane, choose an FSR 3/4 preset, turn **MSAA Off**,
**FXAA Off**, **Frame gen Off**, and **FSR Ghosting Fix — cockpit screens Off**.
Apply graphics, select Native renderer motion, and launch from this app. Load a
flight. The renderer status must say **EXPERIMENTAL native renderer motion (SSR)**
and FSR ACTIVE must be progressing. Merely selecting the option or loading the
layer is not confirmation that this capture is being used.

If the necessary SSR inputs are unavailable, the simulator's native upscaler
continues and the status explains the bypass. There is no silent switch to
camera-derived motion. Select Camera-derived and restart for the previous path.
That path retains the existing experimental MSAA/FG and screen-fix capabilities.

## What changed

The native option reads X-Plane's scene screen-space-reflection pass inputs at
the GPU dispatch: depth, the own-aircraft stencil flag, world and aircraft-local
reprojection, and projection matrices. It reconstructs current-to-previous motion
for the world, aircraft and sky. It uses the aircraft mask instead of the
four-metre cockpit heuristic. Previous projection is kept on the GPU so changing
zoom can produce motion. Applied viewport jitter is removed from reconstruction.
The existing plugin still provides frame timing, jitter sequence, view-cut/reset
signals and camera parameters; it does not own camera/controller input.

A strict SPIR-V classifier checks names, descriptor sets/bindings, member offsets,
matrix layout, scalar types and array strides before reading this private
interface. This is adapted from the MIT [JonathanOrr/xplane-fsr3](https://github.com/JonathanOrr/xplane-fsr3)
project at `fa51577c9bc2c3477e64dfdd358796aebb9ca45b`. Its license is preserved in
`licenses/xplane-fsr3-MIT.txt`. No simulator shaders or GPL motion-mod code are
redistributed. The Windows capture and lifecycle integration is new project code.

Depth, motion and reactive data are written together in one compute dispatch,
followed by a one-thread projection-history update with explicit dependencies.
The existing camera packing pass is skipped. Snapshot images and descriptors are
reused after command-buffer completion. There is no per-frame vector readback or
new device-idle wait. This is an efficiency design, not a measured flight FPS gain.
The previous depth-preservation copy is retained for provenance/compatibility.

## Initial compatibility limits

- Requires a supported scene SSR interface and matching full scene depth in the
  **same one-time primary command buffer** as the FSR boundary, on the single
  application graphics queue. Cross-buffer capture, indirect SSR dispatch, VR,
  MSAA, FG and screen restoration are outside this experiment's active contract.
- A simulator build or rendering path that lacks that interface safely bypasses
  this option. Shader interface recognition alone does not establish live frame
  matching or image quality. X-Plane can change its private rendering interface.
- Aircraft flags describe the own aircraft, not independently moving parts or
  traffic. Props, animated gauges, moving traffic, cloud motion, rain, glass and
  transparency still lack exact object/effect vectors. This is not a universal
  ghosting or stutter fix.
- Fixture GPU checks cover reconstruction, zoom, world/aircraft separation,
  sky, jitter, invalid data, history reset, interface tampering and state restore.
  These do not replace live interior/exterior, panning, weather and long-flight
  tests. This release has not been visually validated in a live X-Plane flight.

## Separate installation and updater channel

Install into the default **XPlaneFSR24-NativeMotion-Experimental** folder, or an
empty separate folder. This setup refuses to overwrite a regular installation.
It uses a separate shortcut and uninstall entry; saved regular choices are not
changed or imported automatically. Close the other launcher before starting a
test. Both launchers use the unchanged owned camera plugin; do not run two
simulators against the same plugin installation simultaneously.

Regular 0.5.8 and older updaters ignore the `-experimental.N` version suffix.
This experimental updater accepts only newer experimental releases. Turning
preview updates Off opts out. Installation still requires the user's click.
Return to the regular release by opening its separate launcher. Keep prior
releases and backups for comparison.
