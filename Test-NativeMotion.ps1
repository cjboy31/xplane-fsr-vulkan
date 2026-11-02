param(
 [switch]$Integration,
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk,
 [string]$AppDirectory=''
)
# Native renderer motion (experimental, v2): SPIR-V interface recognition,
# capture/restore on a real GPU, and (with -Integration) the loader + FSR path
# with a stand-in SSR pass. native-missing must fall back to camera-derived
# motion and say so. No simulator shaders or assets are used.
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$app=if($AppDirectory){[IO.Path]::GetFullPath($AppDirectory)}else{Join-Path $root 'app'}
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$include=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @include (Join-Path $root 'src\native_capture_tests.cpp') '-lbcrypt' '-o' (Join-Path $build 'native_capture_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Native capture test compilation failed'}
$pattern='^(VK_|NATIVE_AA_|XPFSR24_|XP_RENDERER_|XP_TEST_)|^DISABLE_XPFSR24$|^DISABLE_VULKAN_OBS_CAPTURE$'
$previous=@{}
Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {$previous[$_.Name]=$_.Value}
try{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 # Same candidate isolation as Test-FrameGeneration.ps1: no installed implicit copy.
 $implicit=Join-Path $build ('native-empty-implicit-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $implicit | Out-Null
 $env:VK_IMPLICIT_LAYER_PATH=$implicit
 $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
 foreach($array in @(0,1)){
  $shader=Join-Path $build ('native-motion-fixture-'+$array+'.spv')
  & $Glslang '-V' '--target-env' 'vulkan1.2' '-S' 'comp' ('-DINPUT_ARRAY='+$array) (Join-Path $root 'src\native_motion_fixture.comp') '-o' $shader
  if($LASTEXITCODE -ne 0){throw 'Native interface fixture compilation failed'}
  & (Join-Path $build 'native_capture_tests.exe') $shader $array
  if($LASTEXITCODE -ne 0){throw "Native capture checks failed: array=$array"}
 }
 if($Integration){
  if(-not(Test-Path -LiteralPath (Join-Path $build 'layer_test.exe'))){throw 'Run Test.ps1 first to build the loader/FSR integration fixture'}
  $env:VK_LAYER_PATH=Join-Path $app 'layer'
  $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
  $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
  $env:DISABLE_VULKAN_OBS_CAPTURE='1'
  $expectedLayer=[IO.Path]::GetFullPath((Join-Path $app 'layer/XPFSR24Layer.dll'))
  $env:XP_TEST_LAYER_PATH=$expectedLayer
  $env:XP_TEST_LAYER_SHA256=(Get-FileHash -LiteralPath $expectedLayer -Algorithm SHA256).Hash.ToLowerInvariant()
  foreach($provider in @('3.1.6','4.1.2')){
   $env:XPFSR24_PROVIDER=$provider
   foreach($shape in @('native-2d','native-array','native-missing')){
    foreach($mode in @('upscale','native-aa')){
     $log=Join-Path $build ('native-integration-'+$provider+'-'+$shape+'-'+$mode+'.log')
     Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
     $env:XPFSR24_LOG=$log
     & (Join-Path $build 'layer_test.exe') (Join-Path $app 'runtime') (Join-Path $build 'native_test.spv') $mode 1 'attachment' $shape
     if($LASTEXITCODE -ne 0){throw "Native loader/FSR integration failed: $provider $shape $mode"}
     $text=Get-Content -LiteralPath $log -Raw
     if($text -notmatch 'Motion source selected: EXPERIMENTAL native renderer SSR'){throw "Motion source not selected: $provider $shape $mode"}
     if($shape -eq 'native-missing'){
      if($text -notmatch 'Native renderer motion unavailable this frame'){throw "Missing SSR did not report the camera-derived fallback: $provider $mode"}
      if($text -match 'Motion source now native renderer'){throw "Native motion claimed without an SSR pass: $provider $mode"}
     }else{
      if($text -notmatch 'Native renderer motion captured'){throw "SSR inputs were not captured: $provider $shape $mode"}
      if($text -notmatch 'Motion source now native renderer \(SSR, experimental\)'){throw "FSR did not use native motion: $provider $shape $mode"}
     }
    }
   }
  }
 }
 Write-Output 'PASS: native renderer motion recognition, capture, fallback and FSR integration'
}finally{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
}
