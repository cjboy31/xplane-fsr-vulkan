param(
 [switch]$NativeMotion,
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$inc=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @inc (Join-Path $PSScriptRoot 'src\layer_test.cpp') '-o' (Join-Path $build 'layer_test.exe')
if($LASTEXITCODE -ne 0){throw 'Camera safety fixture build failed'}
$names=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_LOG','XPFSR24_STATUS','XPFSR24_CONTROL','XPFSR24_PROVIDER','XPFSR24_SESSION','XPFSR24_MOTION')
$saved=@{}
foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
try{
 $env:VK_LAYER_PATH=Join-Path $PSScriptRoot 'app\layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
 $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 foreach($provider in @('3.1.6','4.1.2')){
  $env:XPFSR24_PROVIDER=$provider
  foreach($mode in @('upscale','native-aa')){
   foreach($case in @('stale-camera','ground-camera')){
    foreach($samples in @(1,4)){
     $path=Join-Path $build ('safety-'+$provider+'-'+$mode+'-'+$case+'-'+$samples+'.log')
     [IO.File]::WriteAllText($path,"STALE-PREVIOUS-SESSION-SENTINEL`n")
     $env:XPFSR24_LOG=$path
     $env:XPFSR24_SESSION=[guid]::NewGuid().ToString('N')
     & (Join-Path $build 'layer_test.exe') (Join-Path $PSScriptRoot 'app\runtime') (Join-Path $build 'native_test.spv') $mode $samples 'attachment' $case
     if($LASTEXITCODE -ne 0){throw "Camera safety failed: $provider $mode $case $samples"}
     $log=[IO.File]::ReadAllText($path)
     if($log.Contains('STALE-PREVIOUS-SESSION-SENTINEL') -or ([regex]::Matches($log,'(?m)^XPFSR24 session=')).Count -ne 1 -or -not $log.Contains('session='+$env:XPFSR24_SESSION)) {throw 'Renderer log did not reset exactly once for this process/session'}
     if($case -eq 'stale-camera' -and -not $log.Contains('Scene camera mismatch:')){throw 'Stale-camera rejection was not logged'}
     if($case -eq 'ground-camera' -and (-not $log.Contains('Ground camera safety entered') -or -not $log.Contains('Ground camera safety released'))){throw 'Ground-camera entry/release was not logged'}
     Write-Output "PASS session isolation: $provider $mode $case MSAA=$samples"
    }
   }
  }
 }
 if($NativeMotion){
  foreach($provider in @('3.1.6','4.1.2')){
   $env:XPFSR24_PROVIDER=$provider
   foreach($shape in @('native-2d','native-array')){
    foreach($mode in @('upscale','native-aa')){
     $env:XPFSR24_LOG=Join-Path $build ('safety-native-'+$provider+'-'+$shape+'-'+$mode+'.log')
     $env:XPFSR24_SESSION=[guid]::NewGuid().ToString('N')
     & (Join-Path $build 'layer_test.exe') (Join-Path $PSScriptRoot 'app\runtime') (Join-Path $build 'native_test.spv') $mode 1 'attachment' $shape 'ground-camera'
     if($LASTEXITCODE -ne 0){throw "Native ground safety failed: $provider $shape $mode"}
     $log=[IO.File]::ReadAllText($env:XPFSR24_LOG)
     if(-not $log.Contains('Ground camera safety entered') -or -not $log.Contains('Ground camera safety released')){throw 'Native ground safety entry/release missing'}
     Write-Output "PASS native ground safety: $provider $shape $mode"
    }
   }
  }
 }
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
