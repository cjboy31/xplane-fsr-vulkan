param(
 [switch]$SkipCpu,
 [switch]$Msaa,
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk,
 [string]$AppDirectory=''
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$app=if($AppDirectory){[IO.Path]::GetFullPath($AppDirectory)}else{Join-Path $root 'app'}
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
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @include (Join-Path $root 'src\layer_test.cpp') '-lbcrypt' '-o' (Join-Path $build 'layer_test.exe')
if($LASTEXITCODE -ne 0){throw 'Layer test compilation failed'}
$pattern='^(VK_|NATIVE_AA_|XPFSR24_|XP_RENDERER_|XP_TEST_)|^DISABLE_XPFSR24$|^DISABLE_VULKAN_OBS_CAPTURE$'
$previous=@{}
Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {$previous[$_.Name]=$_.Value}
try{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 $implicit=Join-Path $build ('layer-empty-implicit-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $implicit | Out-Null
 $env:VK_IMPLICIT_LAYER_PATH=$implicit
 $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
 $env:DISABLE_VULKAN_OBS_CAPTURE='1'
 $env:VK_LAYER_PATH=Join-Path $app 'layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
 $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:XP_TEST_LAYER_PATH=[IO.Path]::GetFullPath((Join-Path $app 'layer/XPFSR24Layer.dll'))
 $env:XP_TEST_LAYER_SHA256=(Get-FileHash -LiteralPath $env:XP_TEST_LAYER_PATH -Algorithm SHA256).Hash.ToLowerInvariant()
 $env:XPFSR24_LOG=Join-Path $build 'layer-test.log'
 Remove-Item Env:XPFSR24_STATUS -ErrorAction SilentlyContinue
 Remove-Item Env:XPFSR24_CONTROL -ErrorAction SilentlyContinue
 $sampleCounts=if($Msaa){@(1,2,4,8)}else{@(1)}
 foreach($samples in $sampleCounts){
 $resolveKinds=if($samples -gt 1){@('attachment','explicit','explicit2')}else{@('attachment')}
 foreach($resolve in $resolveKinds){
 foreach($mode in @('observe','upscale','native-aa')){
  & (Join-Path $build 'layer_test.exe') (Join-Path $app 'runtime') (Join-Path $build 'native_test.spv') $mode $samples $resolve
  if($LASTEXITCODE -ne 0){throw "GPU layer integration test failed: $mode / MSAA $samples"}
 }
 }
 }
 foreach($name in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE','XP_TEST_LAYER_PATH','XP_TEST_LAYER_SHA256')){Remove-Item -LiteralPath ('Env:'+$name) -ErrorAction SilentlyContinue}
 & (Join-Path $app 'NativeAACheck.exe') (Join-Path $app 'runtime') (Join-Path $build 'aa-check')
 if($LASTEXITCODE -ne 0){throw 'Standalone Native AA image quality check failed'}
}finally{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 foreach($name in $previous.Keys){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
