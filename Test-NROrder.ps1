# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,[Parameter(Mandatory)][string]$FsrSdk,
 [Parameter(Mandatory)][string]$AppDirectory)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$app=(Resolve-Path -LiteralPath $AppDirectory).Path
$build=Join-Path $root ('build\nr-order-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $build | Out-Null
$inc=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
foreach($entry in @(@('native_test.comp','comp','native_test.spv'),@('scene.vert','vert','scene.vert.spv'),@('scene.frag','frag','scene.frag.spv'),@('post.frag','frag','post.frag.spv'))){
 & $Glslang -V --target-env vulkan1.2 -S $entry[1] (Join-Path $root ('src/'+$entry[0])) -o (Join-Path $build $entry[2])
 if($LASTEXITCODE -ne 0){throw 'NR order fixture shader compilation failed'}
}
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX @inc (Join-Path $root 'src/layer_test.cpp') -o (Join-Path $build 'layer_test.exe')
if($LASTEXITCODE -ne 0){throw 'NR order GPU test build failed'}
& $Compiler -std=c++17 -O2 -static -shared -DWIN32_LEAN_AND_MEAN -DNOMINMAX @inc (Join-Path $root 'src/nr_order_probe.cpp') -o (Join-Path $build 'XPFSRNeuralRendering.dll')
if($LASTEXITCODE -ne 0){throw 'NR order test backend build failed'}
$names=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_LOG','XPFSR24_STATUS','XPFSR24_CONTROL','XPFSR24_NR_STATUS','XPFSR24_NR_ROOT','XPFSR24_NR_CONTROL','XPFSR24_NR','XPFSR24_FG','XPFSR24_PROVIDER','XPFSR24_MODE','XPFSR_TEST_NR_NATIVEAA')
$previous=@{};foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
try{
 $env:VK_LAYER_PATH=Join-Path $app 'layer';$env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24';$env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:XPFSR24_NR_ROOT=$build
 foreach($provider in @('3.1.6','4.1.2')){foreach($mode in @('upscale','native-aa')){
  $env:XPFSR24_PROVIDER=$provider;$env:XPFSR24_LOG=Join-Path $build ($provider+'-'+$mode+'.log')
  $env:XPFSR24_NR_STATUS=Join-Path $build ($provider+'-'+$mode+'-nr.txt')
  & (Join-Path $build 'layer_test.exe') (Join-Path $app 'runtime') (Join-Path $build 'native_test.spv') $mode 1 attachment nr-order
  if($LASTEXITCODE -ne 0){throw "NR order GPU routing failed: $provider / $mode"}
 }}
 Write-Output "PASS: both NR orders, repeated live switches, NR Off, dirty-scratch bypass/error and recovery on real Vulkan/FSR 3/4 (upscale and Native AA). Evidence: $build"
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}}
