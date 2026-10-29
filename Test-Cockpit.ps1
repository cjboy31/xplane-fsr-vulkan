param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk,
 [string]$Aircraft=''
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
& $Compiler '-std=c++17' '-O2' '-static' '-municode' (Join-Path $root 'src\cockpit_mesh_tests.cpp') '-o' (Join-Path $build 'cockpit_mesh_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Mesh tests compilation failed'}
if($Aircraft){& (Join-Path $build 'cockpit_mesh_tests.exe') $Aircraft}else{& (Join-Path $build 'cockpit_mesh_tests.exe')}
if($LASTEXITCODE -ne 0){throw 'Mesh tests failed'}
$include=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @include (Join-Path $root 'src\cockpit_check.cpp') '-o' (Join-Path $root 'app\CockpitRestoreCheck.exe')
if($LASTEXITCODE -ne 0){throw 'Cockpit GPU tests compilation failed'}
foreach($provider in @('3.1.6','4.1.2')){
 & (Join-Path $root 'app\CockpitRestoreCheck.exe') (Join-Path $root 'app\runtime') $provider (Join-Path $build ('cockpit-check-'+$provider))
 if($LASTEXITCODE -ne 0){throw ('Cockpit GPU tests failed: '+$provider)}
}
