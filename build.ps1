param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [string]$Python='python',
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$XPlaneSdk,
 [Parameter(Mandatory)][string]$FsrSdk,
 [switch]$FrameGenerationDevelopment,
 [switch]$NeuralRenderingDevelopment,
 [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$Compiler=(Resolve-Path -LiteralPath $Compiler).Path
$Glslang=(Resolve-Path -LiteralPath $Glslang).Path
$VulkanHeaders=(Resolve-Path -LiteralPath $VulkanHeaders).Path
$XPlaneSdk=(Resolve-Path -LiteralPath $XPlaneSdk).Path
$FsrSdk=(Resolve-Path -LiteralPath $FsrSdk).Path
$root=$PSScriptRoot
$source=Join-Path $root 'src'
$sourceSnapshot=@{}
Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object {$sourceSnapshot[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
$bin=Join-Path $root 'app'
if($OutputDirectory){$bin=[IO.Path]::GetFullPath($OutputDirectory)}
if($NeuralRenderingDevelopment -and -not $FrameGenerationDevelopment){throw 'NR development requires the explicit Experimental build flag as well'}
$build=Join-Path $root 'build'
[string[]]$channelFlags = if($FrameGenerationDevelopment){@('-DXPFSR_FG_DEVELOPMENT=1')}else{@()}
if($NeuralRenderingDevelopment){$channelFlags+='-DXPFSR_NR_DEVELOPMENT=1'}
New-Item -ItemType Directory -Force -Path $bin,$build,(Join-Path $bin 'layer'),(Join-Path $bin 'auto'),(Join-Path $bin 'plugin\64'),(Join-Path $bin 'runtime'),(Join-Path $build 'generated') | Out-Null
foreach($format in @('16','32')){
 $shaderArgs=@('-V','--target-env','vulkan1.2','-S','comp')
 if($format -eq '32'){$shaderArgs+='-DOUTPUT_RGBA32=1'}
 & $Glslang @shaderArgs (Join-Path $source 'cockpit_restore.comp') '-o' (Join-Path $build ('cockpit'+$format+'.spv'))
 if($LASTEXITCODE -ne 0){throw 'Cockpit shader compilation failed'}
}
& $Python (Join-Path $root 'tools\embed_cockpit_shaders.py') (Join-Path $build 'generated') (Join-Path $build 'cockpit16.spv') (Join-Path $build 'cockpit32.spv')
if($LASTEXITCODE -ne 0){throw 'Cockpit shader embedding failed'}
$resourceCompiler=Join-Path (Split-Path $Compiler -Parent) 'x86_64-w64-mingw32-windres.exe'
Push-Location $source
try{& $resourceCompiler @channelFlags 'app.rc' '-O' 'coff' '-o' (Join-Path $build 'app-icon.o');if($LASTEXITCODE -ne 0){throw 'App icon resource compilation failed'}}finally{Pop-Location}
if(-not(Test-Path -LiteralPath (Join-Path $source 'fsr24.hpp'))){throw 'Missing source adapter: src\fsr24.hpp'}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' (Join-Path $source 'temporal.comp') '-o' (Join-Path $build 'temporal.spv')
if($LASTEXITCODE -ne 0){throw 'Shader compilation failed'}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' '-DDEPTH_MSAA=1' (Join-Path $source 'temporal.comp') '-o' (Join-Path $build 'temporal-msaa.spv')
if($LASTEXITCODE -ne 0){throw 'MSAA packing shader compilation failed'}
& $Python (Join-Path $root 'tools\generate.py') (Join-Path $build 'generated') $VulkanHeaders (Join-Path $build 'temporal.spv') (Join-Path $build 'temporal-msaa.spv')
if($LASTEXITCODE -ne 0){throw 'Header generation failed'}
$includes=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-shared' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @channelFlags @includes (Join-Path $source 'layer.cpp') (Join-Path $source 'layer.def') '-lbcrypt' '-o' (Join-Path $bin 'layer\XPFSR24Layer.dll')
if($LASTEXITCODE -ne 0){throw 'Layer build failed'}
& $Compiler '-std=c++17' '-O2' '-static' '-shared' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' ('-I'+(Join-Path $XPlaneSdk 'CHeaders\XPLM')) (Join-Path $source 'plugin.cpp') (Join-Path $XPlaneSdk 'Libraries\Win\XPLM_64.lib') '-lopengl32' '-lbcrypt' '-ladvapi32' '-lshell32' '-o' (Join-Path $bin 'plugin\64\win.xpl')
if($LASTEXITCODE -ne 0){throw 'Plugin build failed'}
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $source 'integration_setup.cpp') '-lbcrypt' '-ladvapi32' '-lcomdlg32' '-luser32' '-o' (Join-Path $bin 'IntegrationSetup.exe')
if($LASTEXITCODE -ne 0){throw 'Integration setup build failed'}
$aaUpdaterCompiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $aaUpdaterCompiler /nologo /target:winexe /platform:x64 /optimize+ /warnaserror+ /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll (('/win32icon:')+(Join-Path $root 'assets/app.ico')) (('/out:')+(Join-Path $bin 'NativeAAUpdater.exe')) (Join-Path $root 'tools/NativeAAUpdater.cs')
if($LASTEXITCODE -ne 0){throw 'Native AA updater build failed'}
if(Test-Path (Join-Path $source 'launcher.cpp')){
 & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-mwindows' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @channelFlags (Join-Path $source 'plugin_updater.cpp') (Join-Path $build 'app-icon.o') '-lcomctl32' '-lshell32' '-lole32' '-lbcrypt' '-lwinhttp' '-lcomdlg32' '-lgdiplus' '-ldwmapi' '-luxtheme' '-loleacc' '-luuid' '-lmfuuid' '-loleaut32' '-o' (Join-Path $bin 'XPlaneFSR24.exe')
 if($LASTEXITCODE -ne 0){throw 'Launcher build failed'}
}
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @includes (Join-Path $source 'aa_test.cpp') '-o' (Join-Path $bin 'NativeAACheck.exe')
if($LASTEXITCODE -ne 0){throw 'Native AA check build failed'}
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @includes (Join-Path $source 'cockpit_check.cpp') '-o' (Join-Path $bin 'CockpitRestoreCheck.exe')
if($LASTEXITCODE -ne 0){throw 'Cockpit check build failed'}
@'
{
 "file_format_version":"1.2.0",
 "layer":{
  "name":"VK_LAYER_XPFSR24",
  "type":"GLOBAL",
  "library_path":".\\XPFSR24Layer.dll",
  "api_version":"1.4.0",
  "implementation_version":"1",
  "description":"X-Plane FSR SDK 2.4 experimental temporal upscaling",
  "functions":{"vkGetInstanceProcAddr":"XP_GetInstanceProcAddr","vkGetDeviceProcAddr":"XP_GetDeviceProcAddr"}
 }
}
'@ | Set-Content -LiteralPath (Join-Path $bin 'layer\XPFSR24Layer.json') -Encoding utf8
$implicit=Get-Content -LiteralPath (Join-Path $bin 'layer\XPFSR24Layer.json') -Raw | ConvertFrom-Json
$implicit.layer.library_path='..\layer\XPFSR24Layer.dll'
$implicit.layer | Add-Member -NotePropertyName library_arch -NotePropertyValue '64'
$implicit.layer | Add-Member -NotePropertyName disable_environment -NotePropertyValue @{DISABLE_XPFSR24='1'}
$implicit | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bin 'auto\XPFSR24Implicit.json') -Encoding utf8
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @includes (Join-Path $source 'framegen_test.cpp') '-o' (Join-Path $bin 'FrameGenCheck.exe')
if($LASTEXITCODE -ne 0){throw 'Frame generation check build failed'}
foreach($name in @('amd_fidelityfx_loader_vk.dll','amd_fidelityfx_upscaler_vk.dll','amd_fidelityfx_framegeneration_vk.dll')){
 $file=Join-Path $FsrSdk ('Kits\FidelityFX\signedbin\'+$name)
 $signature=Get-AuthenticodeSignature -LiteralPath $file
 if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices'){throw "Invalid AMD signature: $file"}
 Copy-Item -LiteralPath $file -Destination (Join-Path $bin ('runtime\'+$name))
}
New-Item -ItemType Directory -Force -Path (Join-Path $bin 'extras/shadows') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'extras/shadows/make XP shadows great again - enhanced edition.lua'),(Join-Path $root 'extras/shadows/README.txt') -Destination (Join-Path $bin 'extras/shadows') -Force
Copy-Item -LiteralPath (Join-Path $root 'CHANGELOG.md') -Destination (Join-Path $bin 'CHANGELOG.txt') -Force
Write-Output 'Native renderer, in-sim plugin and update-only app built.'
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $bin -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $bin 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALLATION.md') -Destination (Join-Path $bin 'INSTALLATION-AND-USE.txt') -Force
$afterSource=@(Get-ChildItem -LiteralPath $source -File -Recurse)
if($afterSource.Count -ne $sourceSnapshot.Count){throw 'Source files changed during the build. Rebuild before testing or packaging this candidate.'}
foreach($file in $afterSource){if(-not $sourceSnapshot.ContainsKey($file.FullName) -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $sourceSnapshot[$file.FullName]){throw 'Source changed during the build. Rebuild before testing or packaging this candidate.'}}
[ordered]@{scope='Compiler inputs under src; no runtime binaries, user data or model weights';algorithm='SHA256';files=@($afterSource | ForEach-Object {[ordered]@{path=[IO.Path]::GetRelativePath($source,$_.FullName).Replace('\','/');sha256=$sourceSnapshot[$_.FullName].ToLowerInvariant()}})} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bin 'BUILD-SOURCE.json') -Encoding utf8
