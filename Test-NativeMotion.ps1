param(
 [switch]$Integration,
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$include=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @include (Join-Path $PSScriptRoot 'src\native_capture_tests.cpp') '-lbcrypt' '-o' (Join-Path $build 'native_capture_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Native capture test compilation failed'}
$saved=@{}
foreach($key in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_MOTION','XPFSR24_COCKPIT_RESTORE','XPFSR24_FG','XPFSR24_LOG','XPFSR24_STATUS','XPFSR24_CONTROL','XPFSR24_PROVIDER')){
 $saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$null,'Process')
}
try{
 foreach($array in @(0,1)){
  $shader=Join-Path $build ('native-motion-fixture-'+$array+'.spv')
  & $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' ('-DINPUT_ARRAY='+$array) (Join-Path $PSScriptRoot 'src\native_motion_fixture.comp') '-o' $shader
  if($LASTEXITCODE -ne 0){throw 'Native interface fixture compilation failed'}
  & (Join-Path $build 'native_capture_tests.exe') $shader $array
  if($LASTEXITCODE -ne 0){throw "Native capture checks failed: array=$array"}
 }
 if($Integration){
  if(-not(Test-Path -LiteralPath (Join-Path $build 'layer_test.exe'))){throw 'Run Test.ps1 first to build the loader/FSR integration fixture'}
  $env:VK_LAYER_PATH=Join-Path $PSScriptRoot 'app\layer'
  $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
  $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
  foreach($provider in @('3.1.6','4.1.2')){
   $env:XPFSR24_PROVIDER=$provider
   foreach($shape in @('native-2d','native-array','native-missing')){
    foreach($mode in @('upscale','native-aa')){
     $env:XPFSR24_LOG=Join-Path $build ('native-integration-'+$provider+'-'+$shape+'-'+$mode+'.log')
     & (Join-Path $build 'layer_test.exe') (Join-Path $PSScriptRoot 'app\runtime') (Join-Path $build 'native_test.spv') $mode 1 'attachment' $shape
     if($LASTEXITCODE -ne 0){throw "Native loader/FSR integration failed: $provider $shape $mode"}
    }
   }
  }
 }
}finally{foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
