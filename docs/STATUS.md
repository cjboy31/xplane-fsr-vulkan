# 0.5.8 motion optimization

Motion packing and camera algebra are optimized without changing camera/control
ownership or ABI 4. Projection inverses are cached; affine view/body inversion
uses exact double-precision cofactors, with a full inverse fallback. The kernel
projects only the selected world/body transform and avoids external-view eye
reconstruction and per-pixel square roots. Invalid eye transforms reject history.

See [motion-vector research and measurements](MOTION-VECTORS.md). The current
source is still camera-derived with a distance-based cockpit heuristic, not
renderer-native per-object motion. Native SSR inputs are a researched future
backend; they are not integrated or claimed active in this release.

## 0.5.7 custom graphics and MSAA preview

Graphics controls now live on the main screen. Custom editing/apply/restore
preserves FSR and FG providers; pending graphics choices survive app restarts.
Preset selection remains an explicit full configuration and sets FG/MSAA/FXAA Off.
The launcher permits MSAA with FSR and retains the FXAA/native-only launch check.

The renderer preserves 2x/4x/8x source depth samples, tracks resolved scene color
and packs nearest valid geometry depth into single-sample FSR inputs. This needs
a matching dynamic-rendering scene, a transfer-readable depth attachment and a
sampled private image supported by the device. Other simulator paths can bypass;
check live FSR ACTIVE and use MSAA Off when capture is unavailable.

See [MSAA integration](MSAA.md) for rationale, verification and live-test limits.

## Validation status and limitations

Current milestone: 0.5.6-alpha, frame-generation synchronization and temporal history corrections,
with the independent SR22 current-frame Vulkan compositor from 0.5.0.

0.5.6 updates the layer and read-only camera publisher; saved graphics settings
and aircraft coverage remain unchanged. The cockpit-screen fix still supports only
the default Cirrus SR22. See its help for compatibility and validation limits.

The setup checker reads saved preferences only. It checks known AA/scale/output
and monitor keys against the selected mode/preset. It does not change settings,
validate every add-on/driver option or establish compatibility with all hardware.

## Completed checks

- Production motion-packing GPU tests cover ground/sky history, pan sign/scale,
  jitter, invalid depth/projection and excessive motion. CPU tests cover source
  matching, run-ahead/missed/duplicate frames and camera discontinuities.
- FG fixtures read back 88 steady generated images per provider/format run,
  checking for unexpected black output in UNORM and sRGB formats. These fixtures
  do not reproduce the user's live simulator scene or prove scanout quality.
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

The reporter identified FG as the black-flicker trigger. Source fixes address a
real synchronization gap and unsafe frame matching, but the reporter must still
confirm the result in a live flight. See RENDERING-STABILITY.md for the findings
and retest procedure; do not present fixture success as a universal visual fix.
