# Linux FSR 4 and neural rendering pipeline

Added 2026-10-31 at the owner's request. AA/upscaling is independent of frame
generation: Linux AA work is not gated on completing the multi-GPU FG presenter.
This is a development pipeline, not an announcement of a new Linux download.

The existing Linux package remains the FSR 3.1 preview. The Windows 0.7.2 update
does not deliver Linux FSR 4, NR, Native AA or FG parity.

## Tracks and release gates

| Track | Next implementation | Required evidence |
| --- | --- | --- |
| Linux preflight | Build portable UUID/queue/selection tests and Vulkan capability/transfer probes in Ubuntu CI | Passing compile/CPU checks and a capability report; software-runner inventory is not GPU certification |
| FSR 4 AA/upscaling | Identify a supported, redistributable native Linux runtime/backend and add ELF loading/provider discovery | Actual Linux provider query, supported driver/features and HDR/AA readbacks, then X-Plane flight tests |
| NR Linux runtime | Port the project's adapter, context/memory lifecycle and shared-resource ABI onto the pinned upstream Linux Vulkan core | RDNA4 FP8/cooperative-matrix capability checks, supplied-model hash verification and live Off/On cleanup/readback tests |
| In-sim Linux controls | Port session IPC, settings transactions, logs and activation/removal ownership into the native preview | Ordinary/third-party startup, persistence, current-session status, opt-out and uninstall checks |
| Packaging | Produce separate, accurately labeled experimental Linux packages | Licenses, no bundled NVIDIA DLL/model weights, tested install/update/removal and clear hardware/driver requirements |

## FSR 4 dependency status

The supplied AMD SDK 2.4.0 Vulkan runtime payload contains Windows DLLs, not an
ELF `.so` runtime. AMD's current SDK page lists Windows 10/11 as supported OSs.
Vulkan support alone does not make these binaries native Linux libraries. Keep
native FSR 4 unavailable until a supported backend exists and its actual Linux
provider has been exercised. A Wine/Proton path is a separate experiment and
must not be presented as native X-Plane Linux support. Do not relabel FSR 3 as
FSR 4 or redistribute leaked/restricted runtime/model material.

## NR direction

The pinned MIT DLSSNR-AMD reference contains a Linux Vulkan core. Its upstream
game/Proton integrations are not proof of compatibility with native X-Plane.
Port the project's small adapter rather than installing another game's loader
proxies. Preserve the project's pending-GPU ownership and memory-reserve/retry
policies; keep the user's model local. Compare one pass first, Before upscaling,
NR Off and Full scene/cockpit before enabling multi-pass budgets.

Known community targets are Debian with RX 9060 XT, and a separate AI Max 395 /
Radeon 8060S configuration. Their Debian/kernel/Mesa versions are unknown.
Treat their FP8/cooperative-matrix/backend support as unverified; do not infer NR
support from the product name. The 8060S must be assessed separately from RDNA4.

## Continuous checks

`.github/workflows/linux-backend-preflight.yml` compiles the portable GPU queue,
UUID identity, selection, inventory and bounded staging transport infrastructure
with pinned Vulkan headers. It saves only the runner capability report. It
does not download models, run the Windows DLLs, certify FSR 4/NR, generate a Linux
installer or silently enable a backend on users' machines.

Sources: [AMD SDK OS/provider requirements](https://gpuopen.com/amd-fsr-sdk/),
[pinned NR reference](https://github.com/mochizuki0323/DLSSNR-AMD/tree/960038fd1cb892742548f408355e0f3b2fd30d6f),
[current native preview](https://github.com/cjboy31/xplane-fsr-vulkan/releases/tag/native-preview-2026.10.29.1).

Tracking: [Linux pipeline issue #3](https://github.com/cjboy31/xplane-fsr-vulkan/issues/3).
