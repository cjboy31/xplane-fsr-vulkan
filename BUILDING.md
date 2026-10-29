# Building on Windows x64

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
```

`Test-Launcher.ps1` exercises saved presets/provider/checkboxes, quoted/Unicode
arguments, legacy migration and failed-write reporting, plus injection-status
transitions and all 14 reversible graphics presets in workspace fixtures. It
does not start X-Plane or change your simulator preferences.

`Test-Cockpit.ps1 -Aircraft '<your SR22 path>\Cirrus SR22.acf'` additionally checks
the runtime geometry parser against an installed aircraft without copying its
assets into the project. GPU fixtures do not start X-Plane. They cannot verify
real cockpit alignment or the simulator's matrix timing.

Do not install a development build into a live flight. Close X-Plane first and
keep a known-working package. See docs/INSTALLATION.md for the launcher workflow.
