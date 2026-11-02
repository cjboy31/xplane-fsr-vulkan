# Frame-generation activation correction

Private validation is in progress. This is not a published compatibility claim.

## Two independent activation problems

The automatic profile and a second device-startup check previously removed FG
whenever OBS's Vulkan hook was loaded. That avoided a known capture crash but
left the saved On selection different from the running renderer. The in-sim
toggle also saved its choice without writing the renderer's live FG switch.

The new panel distinguishes saved/requested FG, a required restart, panel pause,
startup failures and actual generated/presented counters. The live switch is
available only when startup reserved FG infrastructure. Provider changes still
need a restart. Resuming the live switch resets interpolation history.

## Capture queue ordering

An isolated fixture reproduced an access violation in the installed OBS hook
with the real production layer's queue reservation and SDK swapchain. X-Plane
was closed and was not the crashing process. Simply removing the guard or
draining presentation did not fix that fixture.

[OBS's source](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/vulkan-capture.c)
records queues from the device request it receives, then looks up its present
queue. A layer beneath it can add queues that OBS never saw. This is consistent
with the observed failure. Putting our reservation before capture lets OBS see
the expanded request. The same fixture then passed create, acquire, present,
recreate, old-handle retirement, idle and shutdown with OBS still loaded.

Setup now generates an exact-executable Vulkan loader order, using the
[Khronos loader settings mechanism](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderSettingsFile.md).
The only app key is the selected X-Plane executable. Our implicit layer is
placed first; `unordered_layer_location` retains other discovered layers. This
does not disable OBS globally, edit its manifest or inject another renderer.
Other applications retain normal discovery. Existing competing loader profiles
and edited files are preserved rather than overwritten.

Automatic FG checks the owned profile and loader version (1.4.304 or newer)
before accepting a loaded capture hook. Missing/incompatible setup retains FSR
and reports a repair/restart reason. It does not enter the known crashing path.
The capture profile selects the existing synchronized 3.1.7 presenter while
retaining the chosen FG algorithm (3.1.6 or 4.0.2) and saved preferences.

## Validation so far

- Full CPU/native UI regression passed, including new exact-app/Unicode profile,
  competing-profile refusal, edited-file preservation and owned cleanup cases.
- Production resource fixture passed 64 fenced GPU readbacks, descriptor reuse,
  pressure retirement and pre-recording allocation-failure preservation.
- Both FG algorithms passed 87 generated-image readbacks each, encoded-color
  checks and deliberately missing sources: 90 rendered, 87 generated and 177
  SDK presentations, with three safe real-frame holds per algorithm.
- FG 4.0.2 separately passed the same image checks with the installed OBS hook
  loaded and synchronized presentation. The complete layer lifecycle fixture
  also passed with the exact new renderer and hook loaded.
- Live generation and live Off/On now pass in the corrected build, as detailed
  below. Panel interaction and owner visual/long-flight tests remain open.
  Synthetic tests alone do not establish flight pacing or quality.

## First-frame worker deadlock

The first live session reached FSR and prepared FG once, then stopped advancing.
A bounded stack observation showed the simulator thread inside the SDK's
presentation wait, while an SDK worker waited for our renderer lock in
`L_AcquireNextImageKHR`. Every observed thread was resumed immediately after its
context snapshot; no dump or private simulator state is published.

Native/proxy swapchain routing now copies generation ownership under a separate
short-lived lookup mutex. That mutex is never held across SDK calls, waits or
destruction. Native worker acquire/acquire2/image queries do not need Present's
renderer guard. A regression calls those actual production entry points on a
worker while the guard is deliberately held; all three must finish before it is
released. Proxy ownership and the 64 fenced GPU/resource checks still pass.

Dedicated SDK threads also import other Vulkan commands directly and do not
inherit the caller's thread-local SDK scope. Their first intercepted call is
identified once from the loaded FG module's call stack, outside registry locks.
Those SDK workers then use native routing, including loader dispatch setup,
rather than being tracked as application command recordings. Normal application
threads retain normal interception; explicit SDK calls retain their bounded
scope. The corrected private session is being tested before any release claim.

The next observation found a further wait in `L_CmdPipelineBarrier`: that hook
did not honor SDK scope at all. Auditing the intercepted entry points found the
same omission in synchronization2 barriers and graphics-pipeline creation.
All three now forward SDK work before application tracking or layout rewriting.
The production-function regression now covers six operations under a held
renderer guard, verifies exact SDK barrier layouts and confirms no SDK commands
or layouts were inserted into application tracking. The revised live test passed
the previous freeze point; the incomplete earlier corrections are not release
candidates.

## Corrected live result and remaining work

Windows X-Plane 12.4.4, R9700, 4K full input, FSR 4 Native AA and FG 4.0.2 with
the synchronized 3.1.7 presenter ran beyond the earlier freeze. Thousands of
generated frames advanced. Sampled real/generated images had similar mean
brightness and no detected blank generated frames. This does not prove every
artifact, HDR/VR case or long flight is correct.

- First 20-second observation: 515 reconstruction recordings, 515 generated
  frames and 1,030 SDK presentations. The camera moved during that sample.
- Live FG Off, 10 seconds: 442 reconstruction recordings, no preparations or
  generated frames, 442 SDK presentations.
- Live FG On again, 20 seconds: 510 recordings, 511 generated frames and 1,022
  SDK presentations. Snapshot boundaries can differ by a frame. No restart.
- Saved choices remained unchanged and the original live FG state was restored.
  These counters describe SDK submissions/presents, not physical scanout FPS.

A brief NR+FG test hit the existing memory guard; NR was paused and released,
while FG continued. NR was restored Off. Therefore this session does not prove
NR+FG visual quality. Diagnostics showed roughly 5.3 GiB private frame images
and 8.1 GiB depth captures retained by command buffers. FG's conservative cache
exclusion needs a completion-proven retirement policy before relaxing it. Keep
the OOM reserve and asynchronous presenter lifetime safeguards intact.

The owner has taken over live testing. Do not install/restart, manipulate the
panel or change live controls during that test. This correction is still a
private build; the published release remains unchanged.

SDK callback/context lifetime guards, submitted-source matching, real-frame
fallback, ground-camera protection, encoded-color correction and memory checks
remain in place. AMD requires external serialization of context preparation,
presentation callbacks and destruction; see its
[frame-generation API guidance](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-api/).
