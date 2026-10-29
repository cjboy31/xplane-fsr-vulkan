# Native-motion experimental branch

Build.ps1 also compiles/embeds the native renderer input and projection-history
kernels. Run Test-NativeMotion.ps1 with Compiler, Glslang, VulkanHeaders and
FsrSdk after building. It exercises production hook/capture code for 2D and
array inputs, compares GPU vectors to separately projected points, and checks
interface tampering, bad input and resource/state handling. The layer test accepts
an optional final `native-2d` or `native-array` argument (after mode, sample count
and resolve kind) to exercise the full loader -> SSR capture -> FSR path.
Run the existing tests too: Camera-derived is still the default.

This branch packages version 0.6.0-experimental.1, with a distinct per-user setup
identity and directory. Preserve licenses/xplane-fsr3-MIT.txt in source and
binary packages; native_motion/shader_classify.* and native_motion.comp include
MIT code adapted from JonathanOrr/xplane-fsr3 at the commit recorded in
docs/NATIVE-MOTION-EXPERIMENT.md. No simulator shaders are a build dependency or
part of the source/binary release. Local shader-interface inspection can remain
inside ignored build directories.


# Building on Windows x64

The launcher links the Windows system GDI+, DWM and accessibility libraries for
native styling, DPI handling and field annotations. No browser runtime, bundled
font or third-party UI framework is added. These are launcher-only dependencies;
the rendering layer and publisher do not initialize the UI. Other operating
systems require a port; see [the platform audit](docs/PLATFORMS.md).

Use PowerShell 7 with these dependencies installed locally:

- LLVM-MinGW (UCRT x86_64). Validated compiler: llvm-mingw-20261020, LLVM 23.
- Python 3 for shader embedding/Vulkan metadata generation. Pillow is needed
  only to regenerate the original icon; committed PNG/ICO assets are ready to use.
- glslang standalone compiler.
- Official Vulkan-Headers source checkout, including `include/` and `registry/vk.xml`.
- X-Plane Plugin SDK with `CHeaders/XPLM` and `Libraries/Win/XPLM_64.lib`.
- AMD FidelityFX SDK 2.4.0 with API/upscaler/FG headers and signed Vulkan runtimes.

No dependency is automatically downloaded by the build. Obtain each from its
upstream provider, review its license and pass the local paths explicitly.

```powershell
./build.ps1 -Compiler 'C:\Tools\llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe' `
  -Glslang 'C:\Tools\glslang\bin\glslang.exe' -Python 'python' `
  -VulkanHeaders 'C:\SDKs\Vulkan-Headers' -XPlaneSdk 'C:\SDKs\XPSDK' `
  -FsrSdk 'C:\SDKs\FidelityFX-SDK-2.4.0'
```

The resource compiler is taken from the same LLVM-MinGW `bin` directory.
Generated headers/shaders and objects go to ignored build locations. The build
creates `app/` with the launcher, explicit layer, camera publisher, diagnostic
executables and unmodified signed AMD runtimes. The app icon is embedded in the
launcher executable and is also supplied as source SVG and multi-size ICO.

Run the relevant scripts with the same dependency path overrides:

```powershell
./Test.ps1 -Compiler ... -Glslang ... -VulkanHeaders ... -FsrSdk ...
./Test-FrameGeneration.ps1 -Compiler ... -VulkanHeaders ... -FsrSdk ...
./Test-Cockpit.ps1 -Compiler ... -VulkanHeaders ... -FsrSdk ...
./Test-Launcher.ps1 -Compiler ...
./Test-Updater.ps1 -Compiler ...
./Test-Stability.ps1 -Compiler ... -Glslang ... -VulkanHeaders ... -FsrSdk ...
./Test-CameraSafety.ps1 -Compiler ... -Glslang ... -VulkanHeaders ... -FsrSdk ...
```

`Test-Launcher.ps1` exercises saved presets/provider/checkboxes, quoted/Unicode
arguments, legacy migration and failed-write reporting, plus injection-status
transitions and all 14 reversible graphics presets in workspace fixtures. It
does not start X-Plane or change your simulator preferences.
With `-PreviousPlugin '<verified 0.5.5 win.xpl>'` it also exercises the real
publisher upgrade/backup and unknown-file refusal functions in a fresh workspace
fixture, using the current app payload and an inert simulator executable.

`Test-Updater.ps1` checks release/channel selection and malformed/untrusted
metadata rejection. `-Live` reads this project's public GitHub feed; optional
`-DownloadTo '<new file>'` downloads and verifies a newer installer without
executing it. The launcher links Windows WinHTTP; no third-party updater runtime
is needed. See installer/README.md for packaging and handoff tests.

`Test-Stability.ps1` checks camera/source-frame continuity and executes the
production temporal shader with readback assertions. `Test-FrameGeneration.ps1`
also tests sRGB presentation and checks steady generated frames for black output.
These checks do not establish live-flight quality or frame pacing.

`Test.ps1 -Msaa` also renders Off/2x/4x/8x MSAA scenes through the real layer,
using attachment and explicit color resolves. Repeat with XPFSR24_PROVIDER=4.1.2
for the ML provider. `Test-Stability.ps1` exercises distinct sample depths and
invalid values in both depth conventions. See docs/MSAA.md for capture limits.

`Test-Cockpit.ps1 -Aircraft '<your SR22 path>\Cirrus SR22.acf'` additionally checks
the runtime geometry parser against an installed aircraft without copying its
assets into the project. GPU fixtures do not start X-Plane. They cannot verify
real cockpit alignment or the simulator's matrix timing.

Do not install a development build into a live flight. Close X-Plane first and
keep a known-working package. See docs/INSTALLATION.md for the launcher workflow.

`Test-Stability.ps1` also runs the camera-math equivalence/CPU benchmark. Its GPU
checks compare motion to independently projected 3D points, including zoom and
viewport orientation. `Test-MotionPerformance.ps1 -BaselineSingle '<old.spv>'
-BaselineMsaa '<old-msaa.spv>'` alternates old/new shader order at 1440p/4K and
records timestamp averages; compare per-case medians. Compile the baseline from
v0.5.7-alpha's temporal.comp with/without DEPTH_MSAA=1 using the same glslang.
The current harness must first be built with Test-Stability.ps1. These measurements
cover packing only; they exclude FSR, frame generation and simulator rendering.
