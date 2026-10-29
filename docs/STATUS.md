# Validation status and limitations

Current milestone: 0.5.4-alpha, verified updates and simulator setup guidance,
with the independent SR22 current-frame Vulkan compositor from 0.5.0.

0.5.4 adds launcher updates. Renderer/plugin binaries, saved graphics setting keys
and aircraft coverage are unchanged. The cockpit-screen fix still supports only
the default Cirrus SR22. See its help for compatibility and validation limits.

The setup checker reads saved preferences only. It checks known AA/scale/output
and monitor keys against the selected mode/preset. It does not change settings,
validate every add-on/driver option or establish compatibility with all hardware.

## Completed checks

- Updater fixtures cover stable/preview selection, numeric version ordering,
  downgrade refusal, strict JSON/Unicode and unsafe asset metadata rejection.
- The production WinHTTP client reads the public GitHub release feed.
- The Updates UI and saved preferences were inspected in an isolated Windows
  fixture. Installer checks exercise the automatic-update handoff.
- Setup UI fixture reported mismatched MSAA/FXAA, internal scale and output;
  after supplying matching settings, it reported OK. Resource/preferences hashes
  were byte-identical before and after both read-only checks.
- Default SR22 PFD/MFD geometry parsed at runtime from the user's installation.
- Quad topology, attachment translation, animation rejection, clipping and
  camera-stability CPU tests.
- 96 fenced GPU frames per provider through actual AMD upscaling followed by
  the compositor. Synthetic screen-region MAE decreased from 0.0125956 to
  0.00365557 with SR 3.1.6 and 0.00582961 to 0.00365557 with SR 4.1.2.
- Outside-display and depth-occluded fixture pixels remained byte-identical.
- Existing camera/IPC, diagnostics, AA/upscaling and disable/re-enable regressions.
- Preferences tests cover backups, all 14 FSR 3/4 presets, selective restore,
  duplicate/missing keys, conflict refusal and rollback after a failed write.
- Launcher persistence tests cover all presets/providers, checkboxes, custom
  AA/output, quoted/Unicode arguments, old settings migration, backup metadata,
  read-only failures and corrupt selectors.
- Runtime-status tests distinguish a camera mapping, layer activity, fresh FSR
  recordings/presentation, disabled processing and stale telemetry.
- Windows UI fixture: selected FSR 4 4K Quality from the main screen, verified
  fullscreen 4K/Quality/AA preference edits, help dialogs and advanced controls,
  then closed/reopened the app and confirmed the same preset/provider returned.

The new Windows active notification is requested through the system tray; OS
notification preferences can suppress it. No live-flight notification test or
new live visual-quality result is claimed for this launcher-only update.

These are controlled tests on an AMD Radeon AI PRO R9700, driver 26.10.1.
The supported renderer was investigated on X-Plane 12.4.4-b5, build 124417.

## Not established

Live SR22 attachment/projection alignment and visual improvement for the new
compositor have not been verified. Public matrix datarefs can be cached relative
to command recording; camera motion beyond two output pixels per update bypasses
restoration to limit misalignment. Green alignment tint is provided for inspection.
Depth checks can reject all pixels even when a dispatch was recorded; counters
must not be presented as proof that two screens were visually fixed.

The compositor restores current rendered-scene samples, rather than extracting
the original full-resolution avionics texture. At reduced input resolutions it
cannot recover missing text detail. Edge erosion leaves a small boundary to the
upscaler. Depth tolerances and filtering require more testing.

Frame generation must be Off for this cockpit preview. Generated-frame UI
composition, native avionics-texture recovery, arbitrary aircraft, VR, multiple
windows and complete per-object motion remain future work. Sky/cloud ghosting,
props, glass and scenery are not fixed by a cockpit-only compositor.

SDK validation issues from the FG preview remain unresolved. A real-flight
stutter fix has not been established. Source present-call interval logs are
CPU diagnostics, not monitor scanout measurements or GPU timings.
