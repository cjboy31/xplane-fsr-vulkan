# Community roadmap

## Next implementation priorities

1. Make an original LuminaXP-style in-simulator setup the primary interface:
   an accessible Plugins menu panel, presets/graphics, live NR controls,
   models, diagnostics, guides/updates, persistent settings and early activation
   for direct and third-party launches. Keep the desktop application on GitHub
   as a clearly labeled **optional secondary interface**, sharing saved choices.
   No special desktop launch step should be required after installation. See
   [the integration design and required checks](IN-SIM-PLUGIN.md).
2. After the panel, investigate multi-GPU operation using Lossless Scaling's
   documented behavior as a reference. Evaluate separate render/processing GPU
   selection, cross-device transfer costs, synchronization and fallback. Require
   actual two-GPU validation before advertising compatibility or speed gains.
3. After in-sim integration, investigate the owner's higher CPU frame time.
   Compare one reproducible scene with the mod bypassed, FSR only, NR and FG;
   separate simulator CPU work, driver/queue waits and source/display timing.
   Include the [Agility SDK research findings](AGILITY-SDK-REVIEW.md): prioritize
   measured Vulkan batching, allocation/compilation and NR scratch-memory costs
   before considering a separate Direct3D compute-graph backend.

Neither feature is included in 0.7.1. Implement independently without copying
restricted reference-mod internals.

## Ongoing rendering work

- Verify default SR22 PFD/MFD alignment, depth tolerances and matrix timing in
  stationary, taxi, flight, head movement and resized/FOV-changed views.
- Add a deterministic real-render boundary fixture for the compositor integration.
- Capture clean native avionics textures and UV mappings to restore full output
  detail instead of replaying reduced-resolution scene samples.
- Add proper after-FG UI composition and robust generated-frame occlusion.
- Improve moving-object, propeller, cloud and transparency motion/masks.
- Measure GPU pass cost and source-frame pacing against identical native flights.
- Extend profiles using original parsers/metadata, never embedded aircraft assets.
- Resolve outstanding Vulkan validation problems, especially FG lifecycle/errors.
- Expand aircraft, driver and GPU coverage only after reproducible evidence.
