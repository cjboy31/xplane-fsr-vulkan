param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$includes=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' (Join-Path $PSScriptRoot 'src\temporal.comp') '-o' (Join-Path $build 'temporal-stability.spv')
if($LASTEXITCODE -ne 0){throw 'Production temporal shader failed to compile'}
foreach($name in @('core_tests','temporal_tests')) {
 $exe=Join-Path $build ($name+'.exe')
 $flags=@('-std=c++17','-O2','-static','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX')
 if($name -eq 'temporal_tests'){$flags+='-municode'}
 & $Compiler @flags @includes (Join-Path $PSScriptRoot ('src\'+$name+'.cpp')) '-o' $exe
 if($LASTEXITCODE -ne 0){throw "Stability test compilation failed: $name"}
 $saved=@{}
 foreach($key in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$null,'Process')}
 try {
  if($name -eq 'temporal_tests'){& $exe (Join-Path $build 'temporal-stability.spv')}else{& $exe}
  if($LASTEXITCODE -ne 0){throw "Stability regression failed: $name"}
 }finally{foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
}

& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' '-DDEPTH_MSAA=1' (Join-Path $PSScriptRoot 'src\temporal.comp') '-o' (Join-Path $build 'temporal-msaa.spv')
if($LASTEXITCODE -ne 0){throw 'MSAA production shader failed to compile'}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' (Join-Path $PSScriptRoot 'src\msaa_input.comp') '-o' (Join-Path $build 'msaa-input.spv')
if($LASTEXITCODE -ne 0){throw 'MSAA test input shader failed to compile'}
foreach($samples in @(2,4,8)) {
 & (Join-Path $build 'temporal_tests.exe') (Join-Path $build 'temporal-msaa.spv') $samples (Join-Path $build 'msaa-input.spv')
 if($LASTEXITCODE -ne 0){throw "MSAA production packing failed: $samples samples"}
}
