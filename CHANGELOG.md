# Changes

## 0.5.9-experimental.2

- Bring the regular 0.5.9 session-log reset and ground-camera safeguards to
  both motion choices. Every new simulator process starts fresh diagnostics,
  with one bounded previous-session copy and build/session/PID/UTC headers.
- Temporarily use native rendering when an external camera is within 1.5 m
  of terrain while the aircraft is grounded. Resume FSR at 2.5 m, in the
  cockpit or after takeoff, with temporal history reset. Show GROUND CAMERA
  SAFETY; retain all saved choices. Camera collision and controllers are untouched.
- Preserve recorded secondary-command camera frames and reject stale/conflicting
  depth metadata instead of relabeling it as current. Skip native SSR capture
  during ground safety or when depth provenance is incoherent.
- Add 16 camera-path log/safety Vulkan cases plus eight native-SSR ground-camera
  cases covering both FSR providers, Native AA/upscaling and 2D/array inputs.
  Keep the separate experimental installer identity, updater and opt-in Motion
  selector. Recognize the same updated publisher as the regular 0.5.9 release.

This is a precautionary mitigation for the 0.5.7 ground-pan report, not a live
reproduction or guaranteed cloud/ground artifact cure. Native rendering during
the safeguard changes AA/frame rate. The native-motion option still requires
MSAA, FXAA, FG and cockpit-screen restoration Off and lacks exact independent
object/cloud motion. See docs/NATIVE-MOTION-EXPERIMENT.md for setup and limits.

## 0.5.9-experimental.1

- Add **Native renderer motion — EXPERIMENTAL** as a second main-screen choice.
  Keep Camera-derived as the default; save the choice across restarts, presets
  and custom graphics changes. Restart X-Plane to change the active source.
- Capture scene SSR depth, own-aircraft flags and renderer reprojection at the
  dispatch, with previous projection kept on the GPU for zoom. Skip the camera
  packing pass when this option is active. Reuse owned resources; no per-frame
  vector readback or new GPU idle wait. Preserve upstream MIT attribution.
- Require supported SSR inputs and same-primary-buffer scene provenance. Leave
  native rendering active with an explanatory status when capture is unavailable;
  do not silently switch motion sources. Native option requires MSAA, FXAA,
  frame generation and cockpit-screen restoration Off in this first experiment.
- Use branch `experimental/native-motion-vectors`, an experimental-only update
  channel, and a separate installer folder/identity that refuses regular installs.
  Regular-release updaters do not offer this experimental version automatically.
- Add production-hook GPU vector/depth/history checks, interface tampering,
  state restoration and optional end-to-end FSR fixtures for both image shapes.

This is a community testing build, not a proven fix for all ghosting or stutter.
Moving traffic, props, gauges, clouds, rain and transparency still lack exact
motion. No live-flight visual-quality or FPS improvement is claimed. See
docs/NATIVE-MOTION-EXPERIMENT.md for setup, limitations and return instructions.

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
