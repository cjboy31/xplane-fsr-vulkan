param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk
)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$fixture=Join-Path $build ('public-fg-off-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
foreach($file in @('amd_fidelityfx_loader_vk.dll','amd_fidelityfx_upscaler_vk.dll')){
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('app\runtime\'+$file)) -Destination $fixture
}
$control=Join-Path $fixture 'stale-fg-control.txt'
[IO.File]::WriteAllText($control,'1')
$shader=Join-Path $fixture 'native.spv'
& $Glslang -V --target-env vulkan1.2 -S comp (Join-Path $PSScriptRoot 'src\native_test.comp') -o $shader
if($LASTEXITCODE -ne 0){throw 'Native fixture shader build failed'}
foreach($stage in @('scene.vert','scene.frag','post.frag')){
 & $Glslang -V --target-env vulkan1.2 (Join-Path $PSScriptRoot ('src\'+$stage)) -o (Join-Path $fixture ($stage+'.spv'))
 if($LASTEXITCODE -ne 0){throw "Scene fixture shader build failed: $stage"}
}
$inc=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')))
$exe=Join-Path $fixture 'layer_test.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX @inc (Join-Path $PSScriptRoot 'src\layer_test.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'Public FG-off GPU fixture build failed'}
$names=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XPFSR24_FG','XPFSR24_FG_CONTROL','XPFSR24_EXPECT_NO_FG','XPFSR24_LOG','XPFSR24_PROVIDER','XPFSR24_SESSION','XPFSR24_RUNTIME','XPFSR24_MODE','XPFSR24_CONTROL','XPFSR24_STATUS','XPFSR24_MOTION')
$saved=@{}
foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
try{
 $env:VK_LAYER_PATH=Join-Path $PSScriptRoot 'app\layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24';$env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:XPFSR24_FG='4.0.2';$env:XPFSR24_FG_CONTROL=$control;$env:XPFSR24_EXPECT_NO_FG='1'
 foreach($provider in @('3.1.6','4.1.2')){
  $env:XPFSR24_PROVIDER=$provider
  foreach($mode in @('native-aa','upscale')){
   $env:XPFSR24_SESSION=[guid]::NewGuid().ToString('N')
   $env:XPFSR24_LOG=Join-Path $fixture ($provider+'-'+$mode+'.log')
   & $exe $fixture $shader $mode 1 attachment
   if($LASTEXITCODE -ne 0){throw "Public FG-off FSR regression failed: $provider $mode"}
   if(-not [IO.File]::ReadAllText($env:XPFSR24_LOG).Contains('Frame generation suspended in public build')){throw 'The production layer did not report its FG suspension'}
   Write-Output "PASS: $provider $mode rendered with forced FG=4.0.2 and stale enabled control, no FG provider/runtime and no FG DLL in the fixture."
  }
 }
}finally{foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
