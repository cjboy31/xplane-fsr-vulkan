# AMD denoising feasibility

Reviewed 2026-10-30 against local FidelityFX SDK 2.4.0 and AMD's official
documentation. No denoising toggle or runtime is added in this prototype.

AMD denoisers can improve noisy shadow/reflection or ray-traced lighting
signals when their required raw inputs are available. They are not a generic
fix for temporal ghosting, inaccurate cloud/object motion, jagged silhouettes
or incorrect frame-generation composition. Applying a denoiser to the finished
scene without its guides would not be a faithful integration.

The current capture provides scene color/depth and camera-derived or estimated
motion. It does not identify all of the noisy lighting signals, normal and
roughness/material guides, diffuse/specular albedo and appropriate motion that
Ray Regeneration expects. FSR SDK 2.4.0's denoiser includes a DX12 backend and
sample, not an official Vulkan backend for the existing layer. Its documented
AA/jitter requirements also retain a separate temporal upscaler afterwards.

Before adding this feature, obtain correctly synchronized renderer inputs and
a supported Vulkan backend, document color/material/motion conventions and
validate each noisy signal independently. Keep that work separate from NR's
existing estimated-motion temporal controls. No SDK denoiser DLL is packaged
merely to display an unavailable option.

Primary references:

- [FSR denoising integration](https://gpuopen.com/manuals/fsr_sdk/techniques/denoising/)
- [AMD FSR Ray Regeneration](https://gpuopen.com/amd-fsr-rayregeneration/)
- [FidelityFX Denoiser](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/)
