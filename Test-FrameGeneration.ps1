param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$includes=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX @includes (Join-Path $root 'src\present_test.cpp') -o (Join-Path $build 'present_test.exe')
if($LASTEXITCODE -ne 0){throw 'Swapchain lifecycle fixture compilation failed'}
$names=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_RUNTIME','XPFSR24_FG','XPFSR24_MODE','XPFSR24_LOG','XPFSR24_STATUS','XPFSR24_CONTROL','XPFSR24_FG_CONTROL','DISABLE_VULKAN_OBS_CAPTURE')
$previous=@{}
foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
 foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 foreach($provider in @('3.1.6','4.0.2')){
  $folder=if($provider -eq '4.0.2'){'fg-check-ml'}else{'fg-check'}
  & (Join-Path $root 'app\FrameGenCheck.exe') (Join-Path $root 'app\runtime') (Join-Path $root ('app\'+$folder)) $provider
  if($LASTEXITCODE -ne 0){throw "Moving-scene generation/presentation check failed: $provider"}
  & (Join-Path $root 'app\FrameGenCheck.exe') (Join-Path $root 'app\runtime') (Join-Path $build ($folder+'-srgb')) $provider '--srgb'
  if($LASTEXITCODE -ne 0){throw "sRGB generated-frame black-flash check failed: $provider"}
 }
 $env:VK_LAYER_PATH=Join-Path $root 'app\layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
 $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:DISABLE_VULKAN_OBS_CAPTURE='1'
 $env:XPFSR24_RUNTIME=Join-Path $root 'app\runtime'
 $env:XPFSR24_FG='3.1.6'
 $env:XPFSR24_MODE='native-aa'
 $env:XPFSR24_LOG=Join-Path $build 'presentation-lifecycle.log'
 if(Test-Path -LiteralPath $env:XPFSR24_LOG){Set-Content -LiteralPath $env:XPFSR24_LOG -Value ''}
 & (Join-Path $build 'present_test.exe') $env:XPFSR24_RUNTIME $build
 if($LASTEXITCODE -ne 0){throw 'Layer swapchain create/acquire/present/recreate/retire/idle/destruction check failed'}
 if(-not(Select-String -LiteralPath $env:XPFSR24_LOG -Pattern 'FG SDK presentation proxy ready')){throw 'Fixture bypassed the SDK presentation path'}
 Write-Output 'PASS: both FG providers, moving-image GPU readback, actual additional SDK presents and swapchain lifecycle.'
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}}
