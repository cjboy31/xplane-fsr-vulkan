# Changes

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
