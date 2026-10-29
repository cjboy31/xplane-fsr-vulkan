# MSAA and temporal FSR in 0.5.7

The old launcher blocked MSAA and changed the FSR provider/mode when custom
settings were applied. Its Vulkan layer also accepted only one-sample scene
depth. Removing the launcher check alone would have left FSR waiting for depth.

The new path preserves the source depth sample count (1/2/4/8) in a private copy.
A multisample variant of the production packing shader selects the nearest
valid depth: minimum in normal Z, maximum in reversed Z. It ignores invalid
samples and rejects pixels with no valid sample. FSR and FG still receive the
same single-sample packed depth/motion/color resources. Color must already be
resolved by the simulator. Scene provenance follows dynamic attachment resolves
and explicit vkCmdResolveImage/2/KHR calls, without replacing the simulator's
color resolve or changing image creation flags.

This is an experimental integration choice. AMD's [FSR upscaler documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/)
describes depth/motion inputs and edge reconstruction; it does not certify this
X-Plane capture path or promise additional quality from stacking AA methods.

## Requirements and limits

Use a supported single-window 2D Vulkan scene and the inspected FSR boundary.
Depth needs transfer-source usage, a compatible private sampled format and
matching scene provenance. Unsupported paths bypass FSR; the status explains
MSAA capture failure and suggests Off. No original image usage flags are forced.
The current source depth capture still requires dynamic rendering. Other beta
builds, render passes, transient-only attachments and VR may not meet this contract.

Start with MSAA Off, FSR Native AA at Full input or upscaling below Full, FXAA Off,
and FG Off. Then try 2x MSAA, Apply graphics with X-Plane closed and check for live
FSR ACTIVE. 4x/8x can cost substantially more GPU time and memory; tune base frame
time before adding FG. MSAA does not repair cloud motion, texture shimmer, low
input resolution or bad frame pacing. FXAA remains for native comparison only;
selecting it preserves the FSR/FG choices and the FSR launch explains the mismatch.

Custom editing/applying/restoring does not change FSR/FG selections. Changes to
input scale may require a manual Native AA/upscaling mode change. Pending values
save to settings.ini and are restored on reopening. Simulator writes happen only
on Apply graphics or a launch, with the existing backups and simulator-closed check.

## Verification

The per-sample GPU fixture runs the production shader with distinct sample depths,
normal/reversed Z, partially/all-invalid samples, pan and history cases. Layer
fixtures render multisample depth and resolved color through the actual loader,
check FSR output, post-processing, Native AA and toggles at Off/2x/4x/8x. Launcher
regressions cover all FSR/FG selector combinations and pending/applied persistence.

These tests do not measure real-flight performance, guarantee better edges, or
verify the entire MSAA-plus-FG presentation chain in X-Plane. Reporter testing of
aircraft, weather and external views is still needed. Revert MSAA to Off if FSR
bypasses, performance worsens, or artifacts increase.
