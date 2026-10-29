param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [string]$Python='python',
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$XPlaneSdk,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$source=Join-Path $root 'src'
$bin=Join-Path $root 'app'
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $bin,$build,(Join-Path $bin 'layer'),(Join-Path $bin 'plugin\64'),(Join-Path $bin 'runtime'),(Join-Path $build 'generated') | Out-Null
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
try{& $resourceCompiler 'app.rc' '-O' 'coff' '-o' (Join-Path $build 'app-icon.o');if($LASTEXITCODE -ne 0){throw 'App icon resource compilation failed'}}finally{Pop-Location}
if(-not(Test-Path -LiteralPath (Join-Path $source 'fsr24.hpp'))){throw 'Missing source adapter: src\fsr24.hpp'}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' (Join-Path $source 'temporal.comp') '-o' (Join-Path $build 'temporal.spv')
if($LASTEXITCODE -ne 0){throw 'Shader compilation failed'}
& $Python (Join-Path $root 'tools\generate.py') (Join-Path $build 'generated') $VulkanHeaders (Join-Path $build 'temporal.spv')
if($LASTEXITCODE -ne 0){throw 'Header generation failed'}
$includes=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-shared' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @includes (Join-Path $source 'layer.cpp') (Join-Path $source 'layer.def') '-lbcrypt' '-o' (Join-Path $bin 'layer\XPFSR24Layer.dll')
if($LASTEXITCODE -ne 0){throw 'Layer build failed'}
& $Compiler '-std=c++17' '-O2' '-static' '-shared' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' ('-I'+(Join-Path $XPlaneSdk 'CHeaders\XPLM')) (Join-Path $source 'plugin.cpp') (Join-Path $XPlaneSdk 'Libraries\Win\XPLM_64.lib') '-o' (Join-Path $bin 'plugin\64\win.xpl')
if($LASTEXITCODE -ne 0){throw 'Plugin build failed'}
if(Test-Path (Join-Path $source 'launcher.cpp')){
 & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-mwindows' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $source 'launcher.cpp') (Join-Path $build 'app-icon.o') '-lcomctl32' '-lshell32' '-lole32' '-lbcrypt' '-lwinhttp' '-o' (Join-Path $bin 'XPlaneFSR24.exe')
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
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-Wl,--no-insert-timestamp' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @includes (Join-Path $source 'framegen_test.cpp') '-o' (Join-Path $bin 'FrameGenCheck.exe')
if($LASTEXITCODE -ne 0){throw 'Frame generation check build failed'}
foreach($name in @('amd_fidelityfx_loader_vk.dll','amd_fidelityfx_upscaler_vk.dll','amd_fidelityfx_framegeneration_vk.dll')){
 $file=Join-Path $FsrSdk ('Kits\FidelityFX\signedbin\'+$name)
 $signature=Get-AuthenticodeSignature -LiteralPath $file
 if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices'){throw "Invalid AMD signature: $file"}
 Copy-Item -LiteralPath $file -Destination (Join-Path $bin ('runtime\'+$name))
}
Write-Output 'Native layer, camera plugin and runtime package built.'
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $bin -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $bin 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALLATION.md') -Destination (Join-Path $bin 'INSTALLATION-AND-USE.txt') -Force
