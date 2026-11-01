# Direct3D Agility SDK 1.722 preview review

Reviewed 2026-10-30 against the owner's linked NuGet version and Microsoft's
October 28 release announcement. No Direct3D dependency or preview driver was
added to the application.

The current renderer and independent NR adapter use Vulkan resources and command
buffers. A D3D12 SDK upgrade cannot change those calls or X-Plane's renderer.

| Area | Relevance to this project | Decision |
| --- | --- | --- |
| GPU-timeline descriptor copies | D3D12 descriptor-heap feature, requiring Resource Binding Tier 4 | No direct use in the Vulkan layer; measure our own descriptor/allocation overhead first. |
| DXR geometry/acceleration-structure updates | Need ray-tracing scene geometry and engine integration | Not a way to repair 2D post-process artifacts or add engine ray tracing from our captured inputs. |
| Batched independent commands and fence barriers | Useful scheduling reference; several additions are in the accompanying retail SDK | Evaluate Vulkan batching and dependency scopes. Preserve all producer/consumer dependencies and verify with validation and frame pacing. |
| Compute-graph compilation and matrix operations | Possible future NR backend; support is driver/device dependent | Profile current inference, temporary storage and compilation first. A D3D12 graph backend requires a model port and measured end-to-end comparison. |
| DirectX diagnostics | Useful for a future D3D12 component | Does not replace Vulkan validation, timestamp measurements or the current session log. |

Microsoft's compute-graph specification explains cross-operation fusion and
scratch-memory planning. Those are research directions, not measured gains in
this mod. The current NR reference already uses FP8 cooperative matrix shaders
on the simulator's Vulkan device. Its inference chain has strong dependencies;
putting it on another queue alone does not make the network parallel.

A Windows Vulkan/D3D12 bridge is technically possible through supported external
memory and synchronization handles. The existence of these APIs is not proof
that X-Plane's captured textures have compatible creation flags or that a
particular two-GPU combination supports the required sharing. A prototype would
need format, adapter identity, ownership, synchronization, lifetime, transfer and
latency checks, plus fallback to the native Vulkan path. Copy/synchronization
cost can outweigh a faster network. No cross-API or multi-GPU speedup is claimed.

Next research after in-sim integration: identify measured NR/CPU bottlenecks,
check avoidable allocations and repeated pipeline compilation, consider safe
Vulkan batching and scratch reuse, then evaluate a separate experimental backend
only if the measurements justify it. Keep the working Vulkan release independent
of preview Direct3D runtimes and drivers.

Sources:

- [Microsoft's 1.620 retail / 1.722 preview announcement](https://devblogs.microsoft.com/directx/agilitysdk-1-620-retail-and-1-722-preview/)
- [Microsoft's SDK download/version table](https://devblogs.microsoft.com/directx/directx12agility/)
- [DirectX compute-graph specification](https://microsoft.github.io/DirectX-Specs/d3d/MlirPrograms.html)
- [Khronos external-memory handle reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryHandleTypeFlagBits.html)
- [Khronos synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
- [Owner-linked NuGet package](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.722.0-preview) (web reader could not retrieve the package page; feature claims above use Microsoft's primary documentation).
