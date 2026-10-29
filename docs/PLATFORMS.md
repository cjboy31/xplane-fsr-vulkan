# Platform compatibility

Audit date: October 29, 2026. Native development continues separately on
`experimental/native-platforms`; see [builds, tests and limits](../platform/README.md).
Its Linux FSR 3.1 backend and Mac universal diagnostic probe are previews, not
confirmation of Linux FSR 4 or Mac FSR rendering. A Vulkan API is not by itself proof that a Windows
plugin or its packaged upscaler runtime works on another operating system.

| Platform | Current release | What is established |
| --- | --- | --- |
| Windows x64 | Available, experimental | Native launcher, Vulkan layer, camera publisher and installer. Controlled GPU tests on Radeon AI PRO R9700; live-flight quality remains under evaluation. |
| Linux x64 | No native release; compatibility unconfirmed | X-Plane uses Vulkan, so a native port is feasible. This Windows package is not a Linux plugin. |
| macOS, including Apple Silicon | Unsupported by this implementation | X-Plane uses Metal. This Vulkan interception path cannot attach to that renderer. A separate Metal implementation is required. |

## Linux audit and port work

The current launcher uses Win32 controls, process creation, INI/profile APIs,
WinHTTP and a Windows installer. The publisher uses Windows named mappings,
interlocked IPC and `win.xpl`; the layer loads Windows AMD DLLs and uses Win32
loader/platform definitions. Its FG presentation integration and update pipeline
are also Windows-specific. There are no packaged `lin.xpl`, ELF layer or Linux
AMD provider libraries, Linux installer or Linux test results.

A port needs POSIX shared-memory/lifecycle handling, a native publisher and
launcher, Linux Vulkan loader discovery, a Linux-compatible FSR backend, X11/
Wayland presentation and synchronization work, secure updates and packaging.
The open FSR 3 Vulkan implementation and MIT Linux X-Plane reference project are
possible starting points; they do not establish FSR 4 or frame-generation parity
for this mod. Provider support, licensing and GPU/driver behavior must be verified
on actual Linux hardware. Wine/Proton does not allow native Linux X-Plane to load
this Windows publisher or DLL layer. Running the entire Windows simulator under
compatibility software is a different, untested configuration.

## macOS audit and port work

Laminar documents Metal as the macOS backend. A Vulkan loader layer receives no
rendering commands from X-Plane's native Metal path. MoltenVK translates Vulkan
applications to Metal; adding it does not convert X-Plane's Metal commands into
Vulkan callbacks for this mod.

A Mac version needs a separate Metal renderer integration, native plugin/IPC and
UI, supported reconstruction/interpolation kernels or an Apple-specific backend,
resource lifetime/frame-pacing work, signing and packaging. MetalFX would be an
alternative technology with different capabilities, not confirmation that these
FSR 3/4 providers or FG run on Apple GPUs. Apple Silicon and Intel Mac support
would each need hardware tests. The new SDK's cross-backend Panel Graphics UI
does not by itself expose the scene-depth/motion/presentation integration needed
by this renderer replacement.

## Validation required before claiming support

Build and load the native publisher/layer, verify camera matrices and frame
matching, then test Native AA/upscaling, view/ground transitions, MSAA and provider
failures. Verify actual additional presented frames for FG, pacing, window/
swapchain recreation, shutdown and long flights. Test settings/update/uninstall
preservation, GPU/driver combinations and redistribution terms. A build-only
check or another project's Linux support is insufficient.

## Sources

- [Laminar's 12.4.4 graphics backends](https://www.x-plane.com/2026/09/whats-new-in-12-4-4-the-sdk-update/)
- [AMD FSR SDK and provider requirements](https://gpuopen.com/amd-fsr-sdk/)
- [AMD SDK 2.4 sample build requirements](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.4.0/docs/getting-started/building-samples.md)
- [Khronos Vulkan documentation: macOS/MoltenVK](https://docs.vulkan.org/tutorial/latest/02_Development_environment.html)
- [MIT Linux X-Plane FSR reference](https://github.com/JonathanOrr/xplane-fsr3)

No Linux or Mac compatibility badge or installer is provided by this release.
