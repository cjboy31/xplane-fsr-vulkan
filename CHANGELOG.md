# Changes

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
