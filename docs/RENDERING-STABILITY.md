# Rendering stability and ground-camera safeguards

## 0.5.9: grounded external camera

The 0.5.7 follow-up describes cloud/ground artifacts specifically when the
external camera rests against terrain while the aircraft is on the ground.
It disappears in external airborne views. No fresh log or exact scene is
available, so this is a code investigation and precautionary mitigation.

The publisher now reads the public camera-height-above-ground and aircraft
on-ground datarefs. At external camera clearance <=1.5 m with the aircraft on
the ground, reconstruction and frame generation temporarily bypass to native
rendering. The app reports **GROUND CAMERA SAFETY / Native rendering**. Above
2.5 m, in the cockpit, or after takeoff, normal processing resumes with temporal
history reset. Hysteresis avoids alternating states at the collision floor.
FSR/FG selections remain saved; native AA appearance and frame rate can differ
while active. The app does not move the camera, change collision or read/write
controller inputs. Only cached read-only datarefs are added, without terrain
probes, extra draw callbacks or per-frame GPU readback.

The code audit also found that executing secondary command buffers relabeled
their depth and preserved copies with the latest camera frame. That could
combine an older projection/jitter with current metadata. The layer now retains
the recorded frame and rejects known-stale or conflicting scene-camera stamps.
A controlled stale-secondary regression fails against 0.5.8 and passes the new
layer. This proves the metadata defect, not the reporter's exact visual cause.

Sixteen Vulkan checks cover FSR 3/4, Native AA/upscaling, single-sample/4x MSAA,
stale-secondary rejection, ground entry/hold/release and airborne exclusion.
The fixture also verifies one fresh renderer header per process. CPU tests
cover bounded log rotation, refusal to launch when logs cannot reset, marker
compatibility, invalid-clearance retention and the ground-safety app status.
These checks do not reproduce the reporter's weather, aircraft or camera.

## Session diagnostics

Every new X-Plane launch from the app archives the known diagnostic files as
one `.previous` copy, clears current renderer/status/exit files and stale status
temporary data, and writes a new build/session/PID/UTC header. The Vulkan layer
also truncates its renderer log once when first used by that process. Reopening
the launcher and reloading a flight do not clear the running session's log.
If the app cannot reset logs, it reports the error before launching X-Plane.
Use the current `logs/renderer.log` and status files from the reproducing session;
review private paths before posting. Do not upload simulator/aircraft assets.

## Earlier 0.5.6 corrections

A supplied report showed FSR 4.1.2 upscaling at 1706x960 to 2560x1440, FG 4.0.2
and an sRGB presentation surface. The reporter confirmed black flickering stopped
with FG Off. The log contained active generation but no recorded device-loss or
SDK error proving which frames were black. Ground/external pan artifacts and
airborne jagged edges were also reported. The exact simulator build was not supplied.

## Corrections

AMD's [SDK 2.4 integration requirements](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.4.0/Kits/FidelityFX/docs/techniques/frame-interpolation-api.md#thread-safety)
require external synchronization of presentation, preparation and context
destruction. The layer previously released its guard before SDK presentation.
It now retains the guard for the full operation and serializes game-queue
submission. Context recreation flushes SDK presents and GPU work before destruction.

Source-frame tracking now keeps only one outstanding prepare and enables
generation at presentation only if that source matches the submitted scene.
Preparation no longer configures a future frame for immediate presentation.
Mismatched/menu/run-ahead frames use native presentation; dropped source IDs reset
interpolation. This conservative guard can reduce generated-frame counts rather
than interpolate with the wrong inputs. It does not stall the GPU every frame.

The motion-packing shader previously set full history rejection for all geometry
inside a 20-metre aircraft-centred sphere, including nearby terrain. This mask is
removed. Broad cockpit/sky rejection is reduced to retain useful AA history.
Invalid depth, nonfinite projections and implausible motion are rejected before
reconstruction/FG. The read-only publisher resets history at discontinuities;
it does not take camera control or change input/controller settings.

## Verification and practical limits

Production-shader readbacks check external ground, sky history, pan sign/scale,
jitter cancellation, NaN/infinite depth, behind-camera projection and excessive
motion. CPU tests cover camera continuity, matched/mismatched/duplicate/future
FG sources and IPC. Existing layer image tests cover native/FSR replacement,
toggles, post-processing and analytic AA edges. FG fixtures inspect generated
images using both providers and UNORM/sRGB presentation, with sRGB intermediate
references compared in linear light and source IDs offset from present counts.
The old shader fails the external-ground history regression; the new shader
passes all eight packing cases. Publisher migration from the previous published
binary is tested with a verified backup and refusal to overwrite an unknown file.
These are synthetic results, not the user's
aircraft, terrain, weather or display scanout. Existing SDK validation caveats
and lack of complete moving-object/cloud motion remain.

## Reporter retest

1. Close X-Plane and update the app and its owned camera publisher through the
   launcher. Start with FG Off and the same aircraft/location/view.
2. Pan up/down in external view while stationary on the ground. Compare the
   runway/horizon to the previous build. At the collision floor, confirm
   **GROUND CAMERA SAFETY**; raise the camera above 2.5 m and confirm FSR resumes.
   Repeat after takeoff: the ground safeguard should stay inactive. Check the
   fresh log's safety, camera-frame, reset and FG messages if artifacts remain.
3. Re-enable FG 3 first, then FG 4, using the same route. Verify black flicker
   remains absent; if it returns, turn FG Off and retain the new renderer log.
4. For airborne jagged edges, compare Native AA at Full scale to Quality
   upscaling with MSAA/FXAA Off. Lower input resolution cannot recover all fine
   detail; frame generation does not repair AA or low real frame rates.

The optional cockpit-screen fix still requires FG Off and currently supports
only the default Cirrus SR22. The new build needs the reporter's live confirmation;
it is not a verified universal fix for flicker, stutter, cloud trails or edge shimmer.
