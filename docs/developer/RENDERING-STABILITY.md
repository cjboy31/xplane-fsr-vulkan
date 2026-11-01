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
images using both providers and UNORM/sRGB presentation, with source IDs
offset from present counts. The 0.7.1 encoding regression below supersedes the
earlier assumption that a linear-light sRGB intermediate was presentation-ready.
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


## 0.7.1: current diagnostics and FG fallback

The live log button now reads a bounded current file tail once per second,
with Follow latest and explicit current/last-session labels. Launch rotation
still removes old current-session data. A renderer reload appends to the same
session rather than truncating its evidence. UTC/PID lines, two-second present
heartbeats and actual instance/device lifecycle messages distinguish a bypass
from destruction. Fallback status requires matching PID, session token and an
age of at most three seconds. The activity monitor follows advancing FSR
recordings even if another intercepted pass later publishes a bypass. Missing
telemetry retains prior layer confirmation, with a stale label, not a claim
that the DLL unloaded. No reporter log established an actual periodic unload.

The local official SDK 2.4.0 Vulkan presenter source
`Kits/FidelityFX/framegeneration/fsr3/vk/FrameInterpolationSwapchainVK.cpp`
selects its interpolation output when `numGeneratedFrames > 0`, even after a
failed callback dropped the command list. Our earlier missing-metadata error
left that count at one. The callback now reports zero outputs for missing or
already-consumed source metadata, returns success for this recoverable gap,
and resets generation history on recovery. SDK dispatch errors also set zero
before returning failure; the layer then disables failing FG. The SDK's real
backbuffer continues to present. There is no unsafe re-presentation of an old
swapchain image, extra acquire, semaphore reuse or unsynchronized image copy.
A correct reset dispatch still completes so the SDK can leave reset mode.

[AMD's integration guidance](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
notes that disabling async workloads still leaves UI/presentation on paced
queues. `allowAsyncWorkloads=false` and `onlyPresentGenerated=false` remain
explicit. Thus a black flicker is not proof that this app enabled async FG.
[NVIDIA's current DLSS documentation](https://developer.nvidia.com/rtx/dlss)
requires RTX hardware; Streamline's cross-vendor integration framework does not
make NVIDIA FG a working AMD backend. No NVIDIA FG DLL or support toggle was
added.

`Test-FGFallback.ps1` drives production callbacks through mocked SDK boundaries,
covering normal/reset/missing/duplicate/error and reset recovery. The real
`FrameGenCheck` additionally accepts `--srgb --inject-gaps` after its provider:
three deliberately missing sources in 90 frames gave 87 non-black generated
readbacks and 177 SDK presents on each of 3.1.6 and 4.0.2 (4.0.0 presenter),
including startup/reset outputs. These are controlled fixtures, not evidence
that all live-flight flicker or uneven pacing is fixed. Start with FG Off and
test its visual quality in the user's own simulator/display configuration.

## Display encoding and synchronized compatibility

A later controlled test reproduced a specific sRGB encoding failure. The SDK
samples the sRGB backbuffer through an sRGB view, while its writable output has
no automatic sRGB encode. Its default no-UI presenter directly copies that
output's bytes. The old regression reference accepted those darker linear-light
bytes. Expected static source levels 31/210 became 3/164, producing different
brightness in real/generated frames. This is a confirmed integration defect;
it is not proof that every reported black-screen event has the same cause.

For eight-bit sRGB backbuffers, FrameEncodedColor now raw-copies source bytes
into a private UNORM image and gives the interpolation context UNORM input and
output with the original explicit SDR transfer function. It raw-copies the
encoded result back to the SDK output and restores image states. There is no
unsupported image-view reinterpretation, shader gamma approximation, host
readback, extra present or CPU wait. The pair adds about 63.3 MiB at 4K and two
full-frame GPU copies; the cost in a flight still needs measurement. Actual
HDR/10-bit/ordinary UNORM formats are unchanged. Unsupported callback resource
contracts stop FG with zero advertised output and retain real presentation.

The same executable can expose the old path only in the developer fixture via
`--unadapted-srgb`; it fails the new encoded-level assertion. The production
path passes 31/210, both generation providers, automatic/3.1.7 presenters,
synchronous/async fixture schedules and injected gaps. The app remains
synchronous. The opt-in pixel probe now observes the corrected SDK output.

Optional synchronized compatibility choices in both Windows channels request presenter
3.1.7 and wait for SDK-managed work after each FG-active Present while retaining
the device serialization guard. They deliberately reduce overlap and can lower
performance; they do not replace SDK acquire/semaphore ownership. Ordinary
choices retain automatic presenter selection without this extra wait. Selection
persists and requires restart to change the provider/presentation path.
VSync made the owner's earlier live flicker worse and is not a verified fix.

In the subsequent R9700 / X-Plane 12.4.4-r1 live test with FSR 4, full-scene NR
and FG active, real/generated encoded brightness matched and FG counters kept
advancing. The owner confirmed that visible flicker stopped. This establishes
the correction in that session, not universal hardware or long-flight validation.
