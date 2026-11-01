# Frame-generation reference review

Investigation: 2026-10-30. The owner supplied LuminaXP as a behavioral and
integration reference. This review uses its supplied README, manifests,
runtime-provenance metadata and licence, together with the author's
[public release page](https://forums.x-plane.org/files/file/101816-xp12-luminaxp-dlss-5-frame-gen-vr-multi-view/).
No LuminaXP proprietary implementation or binary is incorporated into this
project. The local reference was not installed alongside the active test layer.

## Confirmed observations and their limits

| Area | Reference evidence | Application here |
| --- | --- | --- |
| Early activation | Implicit-layer manifest, disable environment switch and a small stable shim; README describes registration before the next simulator start | Keep early Vulkan activation separate from the in-sim XPLM controls. See IN-SIM-PLUGIN.md. This is not yet delivered automatic activation. |
| AMD backend | The supplied 1.1.4 provenance identifies official FidelityFX 1.1.4 / FSR 3.1.4 Vulkan. The current public page separately reports SDK 2.4.0 / FSR 3.1.6 | Use our independently sourced official SDK 2.4.0. A local older package is not evidence of the current author's private implementation. |
| NVIDIA compatibility | README and MIT notice identify DLSSG-Transfusion as a separate RTX compatibility path; AMD FG does not use it | Do not treat it as an AMD replacement for NVIDIA FG or bundle NVIDIA components. |
| Timing | README describes ordered real/intermediate frames, source cadence bounded against the display ceiling and rejection of late generated frames | Keep real frames available, protect source identity and test cadence/backpressure. SDK-managed presentation already owns ordering; a separate scheduler must not compete for its queues. |
| Sustained low FPS | README describes stepping down after sustained low source FPS without repeatedly stepping back up | Evaluate a stable source-rate safeguard with explicit status and manual retry; avoid per-frame on/off oscillation. Use AMD's provider-specific guidance rather than copying another backend's thresholds. |
| Evidence | Separate rendered/presented counters and missing-evidence labels are documented | Keep our source and SDK-present counters distinct. Neither proves physical scanout, latency or absence of flicker. |
| Presentation limits | Supplied README targets a main SDR window and declines unsupported HDR/10-bit FG | Test actual format, transfer function and final presentation. Windows HDR being enabled does not by itself identify the application's swapchain format. |

These observations do not establish whether LuminaXP uses a custom AMD
presenter, the SDK replacement swapchain, or particular internal semaphore,
deadline or queue procedures. Its closed package is not source evidence for
those claims. Replicate useful behavior independently, not proprietary code.

## Changes and diagnostic gates

Our layer now configures the current frame before PrepareV2, following
[AMD's integration sequence](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/).
It still checks source submission immediately before Present. A duplicate
configuration for the same frame/enabled state is suppressed. Callback tests
assert both this order and the existing missing/error/duplicate real-frame hold.

`XPFSR24_FG_DIAGNOSTICS=1` enables local subsampled real/generated pixel checks
before SDK composition. Four small staging slots use GPU timestamp availability;
pending slots are skipped instead of adding per-frame GPU waits. Only numerical
summaries enter the local renderer log. The probe distinguishes a largely blank
generated surface with a visible source from a legitimately dark pair. It is
diagnostic evidence, not a full-frame artifact detector or automatic repair.
Unsupported formats are reported, not reinterpreted. It is off by default.

`Test-FGPixelProbe.ps1` checks injected blank surfaces, dark scenes, query-slot
reuse, full-ring skips and unsupported resources on a real Vulkan device.
`FrameGenCheck --srgb --inject-gaps --diagnostics` also exercises the real SDK
callback and generated outputs. All live framegen quality claims require an
in-simulator retest. Prior controlled successes did not eliminate the owner's
reported live flicker; VSync made that report worse.

An additional encoded-level regression then exposed an sRGB mismatch: the old
path produced dark/bright values 3/164 where real frames contained 31/210. The
private-UNORM adapter now preserves 31/210 with both providers. See
RENDERING-STABILITY.md. The earlier nonblack-only and linear-reference checks
were insufficient to verify display encoding.

The optional synchronized compatibility modes now request the official 3.1.7
presenter and drain its work at FG-active Present. Fixture checks passed for
both providers with sRGB, immediate presentation and injected source gaps;
async remains a separate fixture-only experiment. No proprietary scheduler was
copied and this is not asserted to reproduce LuminaXP's private implementation.

Further presentation comparisons must include actual game resolution/format,
normal and compatibility presenters, immediate and FIFO modes, source-frame
gaps, cuts, resize, low FPS and exit. A successful dispatch/readback is not
evidence that the final display path is correct. See
[AMD's presentation guidance](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-swap-chain/).

The app redesign remains subsequent work: an independently built in-sim panel,
clear Graphics/Advanced/Experimental pages and a stable entry point with
versioned, verified payloads. Preserve saved choices and do not add controller
or camera ownership.
