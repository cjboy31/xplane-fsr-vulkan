# Intel graphics development

Experimental source work; no new installed Intel provider selector or release
compatibility claim. Work starts from the protected 0.7.2 Windows source.

## Implemented first milestone

`src/xess_vk_adapter.hpp` loads the unmodified Intel XeSS SDK v3.0.2 runtime
using its documented Vulkan API. It checks the pinned DLL hash, queries instance
extensions/API version before instance creation, device extensions/features before
device creation, asks the SDK for optimal input dimensions and records temporal
upscaling with pixel-space motion, jitter, depth and linear HDR color. SDK headers
and binaries stay external to this repository. Intel's license applies to them.

`XeSSVulkanCheck` creates a separate offscreen device/context, records six frames
and checks four completed FP16 readbacks. The R9700 passed this cross-vendor test
at 226x128 input and 384x216 output with Intel runtime version 2.0.2. SDK package
version and runtime version are separate. This does not certify Intel hardware,
X-Plane integration, moving-view quality, optimal memory use or performance.

Reference SDK: official `intel/xess` tag `v3.0.2`, commit
`8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0`. The supplied AMT 21 archive is a
management SDK and is not a graphics dependency. Installed oneAPI/VTune/ocloc
tools may help later profiling and kernel work; they do not replace XeSS.

## Frame generation paths

Intel's current XeSS-FG guide requires DirectX 12 and XeLL. Its headers expose
a D3D12 swapchain interface, not a native Vulkan FG interface. A future bridge
would need tested cross-API resource ownership, presentation and latency markers;
do not attach a D3D12 swapchain to X-Plane's existing Vulkan device.

The experimental compute-only FSR 3.1.6 FG worker provides a separate Vulkan
candidate for Intel GPUs. Its feature/provider queries and actual Intel device
tests must pass before presenting it as supported. FSR 4/NR availability must
remain separate; an Intel brand name or an available compute queue proves none
of these backends can run. No Intel-specific FG mode is enabled in the release.

## Next gates

1. Test Arc A/B and supported integrated graphics, recording exact driver,
   extensions, formats, budget and provider results. Do not enable a device or
   alter a driver to make a test pass.
2. Merge required features into the layer's private instance/device chains,
   with duplicate-node/core-alias tests and inactive pass-through preserved.
3. Integrate XeSS at the existing reconstruction boundary; match input scale,
   jitter/exposure, previous-minus-current vectors and native fallback.
4. Add saved in-sim provider/quality choices only after the backend records
   correctly. Verify cockpit text, clouds, camera cuts, resize and VRAM pressure.
5. Validate analytical FG on Intel separately, then actual two-physical-GPU
   transfer and presentation. Keep all changes experimental until flight tests.

## Build the fixtures

Run `Build-GPUProbes.ps1` with the Windows compiler, Vulkan headers and optional
`-XessSdk <external XeSS SDK root>` / `-FsrSdk <external AMD SDK root>`.
Use `GpuInventory.exe` to obtain a device UUID. Then run:

```text
XeSSVulkanCheck.exe <absolute path to pinned libxess.dll> <device UUID>
MultiGpuFrameGenCheck.exe <AMD runtime folder> <render UUID> <processing UUID>
```

The explicit `--same-physical-fixture` argument permits two logical devices on
one physical card for the FG fixture. It cannot establish multi-GPU support.

Sources: [Intel XeSS SDK](https://github.com/intel/xess/tree/v3.0.2),
[SR guide](https://github.com/intel/xess/blob/v3.0.2/doc/xess_sr_developer_guide_english.md),
[FG guide](https://github.com/intel/xess/blob/v3.0.2/doc/xess_fg_developer_guide_english.md),
[Intel license](https://github.com/intel/xess/blob/v3.0.2/LICENSE.txt),
[AMD provider requirements](https://gpuopen.com/amd-fsr-sdk/).
