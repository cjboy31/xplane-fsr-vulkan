# Changes

## 0.6.4-alpha

- Replace the ambiguous preview checkbox with a saved **Normal / Experimental**
  release selector under **Updates / Channel**. Normal includes the project's
  alpha/beta/RC releases; the Experimental channel accepts `experimental.N`
  tags that the previous updater rejected.
- Show the installed channel separately from the selected download channel.
  Require an explicit **Switch and install** action for channel changes,
  including switching to a numerically older build. Same-channel updates never
  downgrade or repeatedly reinstall the current version. Preserve user choices.
- Provide a separately compiled Experimental frame-generation build with both
  SDK FG options and diagnostics. Normal retains the enforced FG suspension and
  excludes the FG runtime. This exposes existing development code; no new FG
  algorithm, artifact cure, pacing improvement or flight-quality claim is made.
- Keep the simulator's current output resolution and window mode when launching
  with a saved profile, including native comparison. Apply output only on a
  deliberate preset selection or **Apply graphics**. Keep pending custom edits
  pending until applied. UI scale/font remain under X-Plane's control.
- Add repeated-launch display-preservation, channel discovery, switch-back,
  persistence and installer channel-transition regression coverage. Include a
  skippable guide to the two channels and the revised display workflow.

The display policy removes a repeated launcher override. The report of UI
resizing at the same preset still needs a matched live reproduction; do not
claim every Windows/X-Plane DPI issue is resolved. Experimental FG retains the
previously reported flicker/artifact limitations. Linux/Mac previews unchanged.

## 0.6.4-experimental.1

- Separate opt-in Windows frame-generation development package based on the
  current FSR 3/4 camera/depth correction, with the saved Normal / Experimental
  channel selector and deliberate display-setting behavior from 0.6.4-alpha.
- Enable the existing FSR 3.1.6 and 4.0.2 FG options and include their GPU check
  plus the unmodified, signed AMD SDK frame-generation runtime. Fresh settings
  default FG Off. Cockpit-screen restoration requires FG Off.
- Allow an explicit return to Normal through Updates / Channel. Recognized
  channel switches preserve saved choices; the Normal installer removes the
  two owned FG payload files and enforces FG Off.
- Verify both FG providers with synthetic moving-image GPU readback, additional
  SDK presents and swapchain recreation/retirement tests. These do not establish
  in-flight quality, latency or scanout pacing. No new generation algorithm,
  artifact fix or Lossless Scaling-equivalent quality is claimed. The older
  native-motion experiment remains separate.
- Include the same skippable channel/display feature guide. This package remains
  prerelease; reported FG flicker/artifact limitations have not been resolved.

## 0.6.3-alpha

- Improve FSR 3/4 camera-motion reconstruction by normalizing X-Plane's plugin
  projection to the native Vulkan depth convention before computing world/body
  motion, scene distances and FSR depth parameters. Already-native projections
  pass through once; unsupported matrices safely bypass reconstruction.
- Reject an incompatible older camera publisher and explain that the plugin
  needs updating and X-Plane restarting. The launcher backs up and replaces
  recognized owned publishers; other plugins are not overwritten.
- Validate the correction in X-Plane 12.4.4 with repeated external yaw/lateral
  sweeps, ground-camera transitions and a powered cockpit. Confirm FSR 4 Quality
  reconstructing 2560x1440 to 3840x2160 with the 2x MSAA option selected. Add
  projection regression tests and GPU fallback/recovery coverage for both FSR
  providers. See docs/ARTIFACT-INVESTIGATION.md for scope and evidence.
- Replace clipped HDR comparison sources with genuine FP16 captures and the
  same measured HDR-to-SDR conversion on both sides of the feature guide.
  Keep Skip, replay and existing saved-choice/update behavior.

This is a Windows Vulkan correction, not a universal ghosting cure. Native
rendering still takes over at external ground contact. Independently animated
clouds/objects and other aircraft/add-ons need further validation. Built-in FG
stays suspended; Linux/Mac previews are unchanged. No new GPU passes or runtime
readbacks are added by this correction.

## 0.6.2-alpha

- Add a captioned, locally narrated setup/feature video with a fresh matched
  X-Plane 12.4.4 / FSR 3.1.6 Native AA capture. Clearly label the comparison's
  1080p settings, sequential captures and experimental limits; no FPS or general
  ghosting-fix claim. The renderer algorithms/providers are unchanged.
- Show the verified bundled guide once per installed version. Add immediate
  Skip/Continue, Escape/Close, replay from What's new, Pause and Mute. Remember
  the user's automatic-video/mute choices independently of rendering settings.
- Load the offline Windows decoder only for playback; missing optional media
  components or corrupt/missing media do not prevent normal launcher use.
- Require matching versioned video, transcript, captions, credits and provenance
  during Windows packaging. Preserve settings through updates. Native Linux/Mac
  preview packages are unchanged; Mac remains diagnostics only. Regular FG stays
  suspended and NR stays canceled.

## 0.6.1-alpha

- Temporarily suspend built-in frame generation in regular releases. Show
  **Off — Temporarily suspended**, disable its diagnostic action, and explain
  the suspension through the setting's help. Keep the existing application name.
- Force FG Off at launch and in the Vulkan layer, including old saved choices,
  inherited environment variables and stale FG-control files. The public layer
  uses native presentation and does not create the SDK frame-generation swapchain.
- Exclude the frame-generation runtime and diagnostic executable from fresh
  regular installation packages. Keep FSR 3/4 AA/upscaling, other saved choices,
  cockpit restoration and the ground-camera safeguard. Retain the old FG choice
  in settings for experimental use when saving unrelated settings.
- Keep FG development and its existing release on the separate experimental
  branch. Document the source/pacing/UI work and live validation needed before
  bringing frame generation back, with links to Lossless Scaling's public design
  notes. No Lossless Scaling code, models or binaries are incorporated.

This suspends the reported problematic path; it is not a new interpolation
algorithm or a claim to fix other sources of ghosting/stutter. Existing owned
installations can retain unused files from older releases; the new regular
launcher and layer do not enable them.

## 0.6.0-alpha

- Redesign the native Windows launcher with a charcoal palette, champagne
  accents, rounded cards, clearer typography, custom dropdown/button/toggle
  styling and visible keyboard focus. Keep presets, FSR/FG, MSAA/FXAA, input
  scale and output settings on the main screen.
- Add responsive layout, vertical scrolling at smaller sizes, focus-driven
  scrolling and per-monitor DPI handling. Keep launch actions and live status
  visible in the footer. Annotate native field names for accessibility.
- Restyle help, simulator setup and update windows. Keep real injection states,
  ground-camera safety, saved-choice handling and update integrity checks.
- Add a Platform support action and an evidence-based platform audit: current
  packages are Windows x64 only. Linux is a porting target, not confirmed
  compatible; macOS needs a separate Metal renderer implementation.

The renderer algorithms, motion sources, bundled provider binaries and camera
publisher are unchanged. No Linux/Mac package or new visual-quality claim is
introduced. Existing installer/update identities and saved settings are retained.

## 0.5.9-alpha

- Start each newly launched X-Plane process with fresh renderer, status, exit
  and launch logs. Keep one `.previous` copy, remove stale temporary status,
  and include the build, session ID, process ID and UTC start time. Reopening
  the launcher or reloading a flight does not clear an active session's log.
- Add a ground-camera safeguard for external views while the aircraft is on
  the ground: at camera clearance of 1.5 m or less, temporarily bypass FSR and
  pause frame generation. Resume with a temporal-history reset above 2.5 m,
  inside the cockpit or after takeoff. Show **GROUND CAMERA SAFETY** in the app;
  preserve the user's FSR/FG choices. Camera collision and controls are untouched.
- Preserve the actual recorded camera frame on secondary rendering commands.
  Reject stale or conflicting camera/depth metadata instead of relabeling it
  as current. Add a regression that fails against the previous layer.
- Add 16 real Vulkan layer checks across FSR 3/4, Native AA/upscaling and
  single-sample/4x MSAA, plus log-rotation and ground-clearance hysteresis tests.
  Recognize and back up the previous owned publisher when installing the new
  read-only camera-clearance metadata publisher.

The ground-camera change is a precautionary mitigation for the 0.5.7 report;
the reporter's exact scene has not been reproduced. Native rendering near the
ground can look different and pauses generated frames. This does not supply
missing cloud/object motion or guarantee that all artifacts are eliminated.
The alternative native-motion backend remains on its separate experimental
branch and update channel.

## 0.5.8-alpha

- Reduce motion packing to one selected world/body reprojection. Reconstruct
  eye position only inside the cockpit, using a squared-distance test without
  a per-pixel square root/divide. Preserve the FSR current-to-previous convention.
- Cache unchanged projection inverses; factor perspective and affine view/body
  transforms in double precision. Remove the unused aircraft-sphere dataref reads.
- Reject invalid internal-view eye transforms instead of retaining history with
  an unusable camera matrix. Keep existing view-cut and invalid-motion guards.
- Add independent perspective scene-flow tests for pan/rotation, zoom, body/world
  selection, normal/reverse Z and viewport orientation, plus repeatable GPU timing
  and large-world camera-math tests. Preserve ABI 4 and owned-plugin upgrades.

On the development R9700, five alternating-order paired runs measured about
8-15% less GPU packing time (1440p/4K, single-sample/4x MSAA, exterior/interior).
At 4K without MSAA the pass measured about 0.067/0.083 ms exterior/interior.
These synthetic pass timings are not a flight FPS gain. Complete moving-object,
propeller, cloud and transparency vectors remain unavailable; see docs/MOTION-VECTORS.md.

## 0.5.7-alpha

- Move MSAA, FXAA, input scale, output resolution and Apply graphics onto the
  main screen. Remove the Advanced graphics window.
- Keep FSR and frame-generation selections when switching to Custom, editing,
  applying or restoring graphics. Remember pending edits across app restarts.
- Add experimental 2x/4x/8x MSAA depth capture and nearest-depth resolve for FSR
  3/4. Track dynamic-rendering color resolves and explicit resolve commands.
  The SDK still receives single-sample inputs. Unsupported captures bypass FSR
  and suggest MSAA Off. FXAA remains native-only and never silently resets FSR/FG.
- Add per-sample GPU resolve checks in both depth conventions, MSAA layer
  integration scenes, and custom-edit/provider persistence regressions.

MSAA can increase GPU cost and does not fix cloud/texture aliasing. Synthetic
checks do not prove live-flight quality or the full MSAA + FG presentation path.
Use MSAA Off for the first test; try 2x next and check live FSR ACTIVE status.

## 0.5.6-alpha

- Serialize SDK frame preparation, presentation and game-queue submissions to
  remove a frame-generation race. Flush generated work before context recreation.
- Match interpolation to the submitted source scene, skip mismatched/run-ahead
  frames and prevent one prepared frame from being generated twice.
- Remove the external-view 20-metre aircraft history mask, which also covered
  runway and terrain. Retain more cockpit/sky history for temporal antialiasing.
- Reject invalid depth/motion samples and reset history on camera cuts, abrupt
  zooms, view/projection changes and teleports. Add periodic view/depth diagnostics.
- Add GPU packing regressions and generated-frame readback checks for both FG
  providers in UNORM and sRGB formats, including the format in the reported log.
- Recognize and back up the previous published camera plugin during upgrade;
  verify migration without starting or changing a real simulator installation.

These address concrete stability/history defects. The reporter confirmed that
turning FG Off stops the black flicker; the fixes still require their live-flight
retest. Complete object/cloud motion and all ghosting/jagged edges are not solved.

## 0.5.5-alpha

- Name the application FSR 3/4 + Frame Gen for X-Plane. Update launcher titles,
  tray text, executable metadata, setup, shortcuts, guide and release branding.
- Keep FSR Ghosting Fix as the cockpit-screen feature label, with its existing
  default Cirrus SR22 support and frame-generation-Off requirement explained in help.
- Prefer the newly named installer in the updater. Publish an identical legacy
  asset for 0.5.4 users so automatic upgrades continue to work.

Rendering and saved setting/installation identities are unchanged.

## 0.5.4-alpha

- Add automatic GitHub Release checks on startup and an Updates window with
  remembered automatic-check and preview-channel choices.
- Offer Download and install after a newer compatible release is found. Verify
  HTTPS origin, file size and GitHub's SHA-256 digest before running setup.
- Update the recognized installation in place, wait for the old launcher to exit,
  refuse locked application files and reopen the launcher with saved choices.
- Publish a standalone installer alongside the RAR for future in-app updates.
  Users of older versions must install this updater-enabled version once.

Installation requires the user's click and X-Plane/GPU checks closed. There is
no background service, forced mid-flight update or renderer change in this build.

## 0.5.3-alpha

- Add Sim setup on the main screen with a first-flight checklist and an explicit
  Native AA / Quality / Performance settings table.
- Explain the difference between simulator input scale, FSR provider and final
  output resolution, including 1440p input to 4K output.
- Add a read-only saved-settings report for AA, scale, output and monitor
  mismatches; direct users to presets/Apply and save while X-Plane is closed.
- Expand installation instructions with first-test options and live-status
  verification. Explain that X-Plane 12 already uses Vulkan on Windows.

Launcher/documentation update only. Renderer and camera publisher behavior are
unchanged; no new camera/controller fix or live-flight quality result is claimed.

## 0.5.2-alpha

- Rename the app to FSR Ghosting Fix for X-Plane and the cockpit option to
  FSR Ghosting Fix - cockpit screens. Use the same naming in the guide, metadata
  and diagnostics messages.
- Call the separate compatibility option FSR 3 history tuning to distinguish it
  from cockpit-screen restoration.
- Keep current aircraft support and FG-Off requirements explicit in help/docs.
  Only the default Cirrus SR22 cockpit-screen profile is currently supported.

Naming update only: rendering, settings keys, aircraft support and the saved
choices behavior are unchanged from 0.5.1. No new ghosting/stutter fix is claimed.

## 0.5.1-alpha

- Save launcher choices immediately and restore them on startup. Preserve legacy
  folder settings and graphics-backup metadata; report save failures explicitly.
- Move presets to the main screen and add seven matching FSR 4 ML-preview presets.
  Recheck/reapply saved graphics before launching FSR; keep selective backups.
- Add help beside settings, a wider two-column layout and automatic FG/cockpit
  compatibility adjustments.
- Add live injection states and request a one-time Windows notification only
  after recent FSR recordings and Vulkan presentation. Stale data is not active.
- Add persistence, status-transition and FSR 4 preset regression fixtures.

This release changes the launcher; it does not establish a live ghosting/stutter
fix or remove the SR22 compositor's FG-Off limitation.
