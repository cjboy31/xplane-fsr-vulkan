param(
 [switch]$SkipCpu,
 [switch]$Msaa,
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$include=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
if(-not $SkipCpu){
& $Compiler '-std=c++17' '-O2' '-static' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $root 'src\core_tests.cpp') '-o' (Join-Path $build 'core_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Core tests compilation failed'}
& (Join-Path $build 'core_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Core tests failed'}
}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' (Join-Path $root 'src\native_test.comp') '-o' (Join-Path $build 'native_test.spv')
if($LASTEXITCODE -ne 0){throw 'Test shader compilation failed'}
foreach($stage in @('vert','frag')){
 & $Glslang '-V' '--target-env' 'vulkan1.2' '-S' $stage (Join-Path $root ('src\scene.'+$stage)) '-o' (Join-Path $build ('scene.'+$stage+'.spv'))
 if($LASTEXITCODE -ne 0){throw 'Scene fixture shader compilation failed'}
}
& $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'frag' (Join-Path $root 'src\post.frag') '-o' (Join-Path $build 'post.frag.spv')
if($LASTEXITCODE -ne 0){throw 'Post-process fixture shader compilation failed'}
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @include (Join-Path $root 'src\layer_test.cpp') '-o' (Join-Path $build 'layer_test.exe')
if($LASTEXITCODE -ne 0){throw 'Layer test compilation failed'}
$names=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_LOG','XPFSR24_STATUS','XPFSR24_CONTROL','XPFSR24_MODE')
$previous=@{}
foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
 $env:VK_LAYER_PATH=Join-Path $root 'app\layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
 $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:XPFSR24_LOG=Join-Path $build 'layer-test.log'
 Remove-Item Env:XPFSR24_STATUS -ErrorAction SilentlyContinue
 Remove-Item Env:XPFSR24_CONTROL -ErrorAction SilentlyContinue
 $sampleCounts=if($Msaa){@(1,2,4,8)}else{@(1)}
 foreach($samples in $sampleCounts){
 $resolveKinds=if($samples -gt 1){@('attachment','explicit','explicit2')}else{@('attachment')}
 foreach($resolve in $resolveKinds){
 foreach($mode in @('observe','upscale','native-aa')){
  & (Join-Path $build 'layer_test.exe') (Join-Path $root 'app\runtime') (Join-Path $build 'native_test.spv') $mode $samples $resolve
  if($LASTEXITCODE -ne 0){throw "GPU layer integration test failed: $mode / MSAA $samples"}
 }
 }
 }
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}}
$savedLayers=@{}
foreach($name in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE')){$savedLayers[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
try{
 & (Join-Path $root 'app\NativeAACheck.exe') (Join-Path $root 'app\runtime') (Join-Path $root 'app\aa-check')
 if($LASTEXITCODE -ne 0){throw 'Standalone Native AA image quality check failed'}
}finally{foreach($name in $savedLayers.Keys){[Environment]::SetEnvironmentVariable($name,$savedLayers[$name],'Process')}}
