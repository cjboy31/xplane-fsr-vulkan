# Laminar's roadmap and using more of the PC (research, 2026-11-02)

Shared by the FSR mod and Native AA. What X-Plane is changing, what it means
for a Vulkan layer that replaces or adds passes, and which hardware ideas can
actually make the sim faster.

## What Laminar shipped and announced

| Version / timing | Change | Effect on our mods |
| --- | --- | --- |
| **12.4.4 "SDK Update"**, final Oct 29 2026 | FSR now applies AMD's recommended texture LOD bias at all quality levels | X-Plane's FSR is FSR 1. AMD's FSR 2/3/4 docs (in the SDK we ship against) recommend `log2(render/display) - 1.0`, one mip level sharper than an FSR 1-style bias. If X-Plane applies the FSR 1-style value (to confirm from the log's original bias values), our temporal FSR shows textures one level blurrier than AMD intends. **Implemented: opt-in extra bias (below).** |
| 12.4.4 | Hybrid tiled lighting for night lights; night-light flicker fixes in dense areas | Changes the night scene both mods measure (AA check 5, Sharper night lights). Re-measure on 12.4.4 |
| 12.4.4 | Sparse residency for LIT textures; managed buffers split into resident regions; `--gfx_no_defragment`; defragmenter stutter fix | X-Plane's device now may ask for sparse features/queues. Layers pass `vkQueueBindSparse` through and clone feature chains from the registry, so no change is needed; check the device-creation log on AMD Linux |
| 12.4.4 | On-disk texture cache (0-10 GB) | X-Plane already uses spare disk/RAM for texture preprocessing; nothing for a mod to add |
| 12.4.4 | Zink updated to Mesa 25.3.4; Zink startup assertion fixed | Re-run both Zink matrix checks on 12.4.4 |
| 12.4.4 | **Panel Graphics API** (Vulkan avionics/windows, MSDF text, batching, record-and-replay), native ImGui and CEF; SDK 4.4.0 RC1 | Cockpit displays drawn with Panel Graphics are rendered by X-Plane in Vulkan, not through Zink. The FSR cockpit-screen restore must be checked against such aircraft. Our own panels can move to Panel Graphics: Ben Supnik writes that OpenGL plugin drawing "requires expensive bridging and is impossible to make multi-core" |
| 12.4.4 | Cockpit panels sharper (less instrument blur); per-OBJ "no FXAA" flag | Compare cockpit text with and without each mod again |
| 12.5.0, Q4 2026 | Flight-model update; ImGui support listed for 12.5 | No rendering impact expected |
| Late 2026 | Tile scenery engine replacing DSF (streaming, LODs, GPU-friendlier) | Expect new terrain passes; both mods identify X-Plane's passes by structure and bypass on mismatch, so plan a classifier/LDR re-check on its first beta |
| Future (no date) | Plugin rendering on other threads through X-Plane's thread pool | Would let panels draw off the main thread; follow the SDK |

## Using other hardware to make the sim faster

X-Plane's frame rate is usually limited by its main thread, which records all
rendering into one command buffer. A mod cannot move X-Plane's own simulation
or culling onto the GPU. What it can do:

| Idea | Status |
| --- | --- |
| Keep our own CPU work small and off the main thread | Done: per-frame timers, worker-thread FSR start-up, background file worker |
| Use spare CPU cores well | Done: opt-in CPU scheduling helper (P-cores, X3D cache chiplet, no EcoQoS) |
| Trade GPU time for CPU time | Done: upscaling and frame generation. FG doubles displayed frames without more sim CPU work |
| Lower latency at the same FPS | Done for AMD (Anti-Lag 2). NVIDIA's `VK_NV_low_latency2` (Reflex for Vulkan) is the equivalent; queued |
| More texture detail for no CPU cost | **Implemented now**: the AMD-recommended extra mip bias for temporal upscaling / TAA, opt-in |
| Run our passes on an async compute queue | Queued (P3): needs timer data showing GPU overlap room. X-Plane already uses async compute itself on Windows |
| Second GPU (or iGPU) for frame generation | Queued (P3): PCIe copy of every frame; needs two-GPU hardware to measure |
| Spare RAM / disk for textures | X-Plane 12.4.4 does it (texture cache, managed buffers). Nothing to add |
| Panel drawing in Vulkan instead of OpenGL | Queued (P2): move both in-sim panels to the Panel Graphics API once SDK 4.4 is final |

## Sources

- X-Plane 12.4.4 release notes: https://www.x-plane.com/kb/x-plane-12-4-4-release-notes/
- What's new in 12.4.4: https://www.x-plane.com/2026/09/whats-new-in-12-4-4-the-sdk-update/
- SDK roadmap (UI and avionics), B. Supnik, Apr 24 2026: https://developer.x-plane.com/2026/04/x-plane-sdk-roadmap-user-interface-and-avionics/
- FSExpo 2026 roadmap: https://thresholdx.net/?p=43771 and https://fsnews.eu/x-plane-announces-new-sdk-panel-graphics-and-scenery-update-at-fsexpo-2026/
- AMD FidelityFX SDK, `super-resolution-upscaler.md` / `super-resolution-ml.md`, "Mipmap biasing" (`mipBias = log2(renderResolution/displayResolution) - 1.0`)
- VK_NV_low_latency2: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_NV_low_latency2.html
