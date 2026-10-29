# Validation status and limitations

Current milestone: 0.5.0-alpha, independent SR22 current-frame Vulkan compositor.

## Completed checks

- Default SR22 PFD/MFD geometry parsed at runtime from the user's installation.
- Quad topology, attachment translation, animation rejection, clipping and
  camera-stability CPU tests.
- 96 fenced GPU frames per provider through actual AMD upscaling followed by
  the compositor. Synthetic screen-region MAE decreased from 0.0125956 to
  0.00365557 with SR 3.1.6 and 0.00582961 to 0.00365557 with SR 4.1.2.
- Outside-display and depth-occluded fixture pixels remained byte-identical.
- Existing camera/IPC, diagnostics, AA/upscaling and disable/re-enable regressions.
- Prior 0.4.x preferences tests cover backups, seven presets, selective restore,
  duplicate/missing keys, conflict refusal and rollback after a failed write.

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
