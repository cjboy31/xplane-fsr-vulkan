# Rendering stability changes in 0.5.6

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
   runway/horizon to the previous build. Check the new renderer log's view,
   reset, projection/depth and FG source/bypass messages if artifacts remain.
3. Re-enable FG 3 first, then FG 4, using the same route. Verify black flicker
   remains absent; if it returns, turn FG Off and retain the new renderer log.
4. For airborne jagged edges, compare Native AA at Full scale to Quality
   upscaling with MSAA/FXAA Off. Lower input resolution cannot recover all fine
   detail; frame generation does not repair AA or low real frame rates.

The optional cockpit-screen fix still requires FG Off and currently supports
only the default Cirrus SR22. The new build needs the reporter's live confirmation;
it is not a verified universal fix for flicker, stutter, cloud trails or edge shimmer.
