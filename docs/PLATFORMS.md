# Platform compatibility

Audit date: October 30, 2026. A Vulkan API is not by itself proof that a Windows
plugin or its packaged upscaler runtime works on another operating system.

| Platform | Current release | What is established |
| --- | --- | --- |
| Windows x64 | Available, experimental | Native launcher, Vulkan layer, camera publisher and installer. Controlled GPU tests on Radeon AI PRO R9700; live-flight quality remains under evaluation. |
| Linux x64 | Separate FSR 3.1 preview; real-flight compatibility unconfirmed | The native-platform branch supplies a Vulkan backend and native package. It does not include Windows FSR 4/NR/FG parity. |
| macOS, including Apple Silicon | Separate universal diagnostic probe only | Native plugin loading and camera diagnostics are implemented. No Metal FSR, NR, FG or AA enhancement is implemented. |

The [published native preview](https://github.com/cjboy31/xplane12-fsr-framegen/releases/tag/native-preview-2026.10.29.1)
already provides a Linux archive, a Mac Universal Probe ZIP, instructions and a
combined RAR. The Mac package is a diagnostic installation, not a rendering-mod
installer. Both architectures are built; this does not establish a real flight
on every Mac. No new full-feature Mac installer is offered until a Metal backend
exists and passes native simulator tests.

## Linux audit and port work

The current launcher uses Win32 controls, process creation, INI/profile APIs,
WinHTTP and a Windows installer. The publisher uses Windows named mappings,
interlocked IPC and `win.xpl`; the layer loads Windows AMD DLLs and uses Win32
loader/platform definitions. Its FG presentation integration and update pipeline
are also Windows-specific. This Windows package contains no `lin.xpl`, ELF
layer or Linux AMD provider libraries. The separate native preview supplies its
own Linux implementation, synthetic/native-host test results and installation
tools; these do not establish real-flight compatibility or current Windows parity.

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

The Windows release supplies no native Linux/Mac installer. Use the explicitly
labeled separate preview above; a Mac diagnostic probe must not be advertised
as working FSR/NR/FG rendering.
