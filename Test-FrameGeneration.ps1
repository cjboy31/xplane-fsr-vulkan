param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk,
 [string]$AppDirectory='',
 [string]$RuntimeDirectory='',
 [string]$RejectedLayer=''
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$app=if($AppDirectory){[IO.Path]::GetFullPath($AppDirectory)}else{Join-Path $root 'app'}
$runtime=if($RuntimeDirectory){[IO.Path]::GetFullPath($RuntimeDirectory)}else{Join-Path $app 'runtime'}
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$includes=@(('-I'+(Join-Path $build 'generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\api\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\upscalers\include')),('-I'+(Join-Path $FsrSdk 'Kits\FidelityFX\framegeneration\include')))
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX @includes (Join-Path $root 'src\present_test.cpp') -lbcrypt -o (Join-Path $build 'present_test.exe')
if($LASTEXITCODE -ne 0){throw 'Swapchain lifecycle fixture compilation failed'}
$pattern='^(VK_|NATIVE_AA_|XPFSR24_|XP_RENDERER_|XP_TEST_)|^DISABLE_XPFSR24$|^DISABLE_VULKAN_OBS_CAPTURE$'
$previous=@{}
Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {$previous[$_.Name]=$_.Value}
try{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 # The loader de-duplicates names before enable/disable filtering. An installed
 # implicit manifest can therefore hide this candidate's explicit manifest even
 # with ~implicit~ disabled. Override implicit discovery only in fixture children.
 $implicit=Join-Path $build ('fg-empty-implicit-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $implicit | Out-Null
 $env:VK_IMPLICIT_LAYER_PATH=$implicit
 $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
 foreach($provider in @('3.1.6','4.0.2')){
  $folder=if($provider -eq '4.0.2'){'fg-check-ml'}else{'fg-check'}
  & (Join-Path $app 'FrameGenCheck.exe') $runtime (Join-Path $build $folder) $provider
  if($LASTEXITCODE -ne 0){throw "Moving-scene generation/presentation check failed: $provider"}
  & (Join-Path $app 'FrameGenCheck.exe') $runtime (Join-Path $build ($folder+'-srgb')) $provider '--srgb'
  if($LASTEXITCODE -ne 0){throw "sRGB generated-frame black-flash check failed: $provider"}
 }
 $env:VK_LAYER_PATH=Join-Path $app 'layer'
 $env:VK_INSTANCE_LAYERS='VK_LAYER_XPFSR24'
 $env:VK_LOADER_LAYERS_ENABLE='VK_LAYER_XPFSR24'
 $env:DISABLE_VULKAN_OBS_CAPTURE='1'
 $env:XPFSR24_RUNTIME=$runtime
 $env:XPFSR24_FG='3.1.6'
 $env:XPFSR24_MODE='native-aa'
 $expectedLayer=[IO.Path]::GetFullPath((Join-Path $app 'layer/XPFSR24Layer.dll'))
 # DISABLE_XPFSR24 disables explicit activation too: keep it cleared for the
 # candidate. Discovery isolation above excludes installed implicit copies.
 $env:XP_TEST_LAYER_PATH=$expectedLayer
 $env:XP_TEST_LAYER_SHA256=(Get-FileHash -LiteralPath $expectedLayer -Algorithm SHA256).Hash.ToLowerInvariant()
 if($RejectedLayer){
  $old=(Resolve-Path -LiteralPath $RejectedLayer).Path
  if((Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash -eq $env:XP_TEST_LAYER_SHA256){throw 'Negative control must use a different renderer build'}
  $negative=Join-Path $build ('fg-explicit-negative-'+[guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $negative|Out-Null
  $manifest=Get-Content -LiteralPath (Join-Path $app 'layer/XPFSR24Layer.json') -Raw|ConvertFrom-Json
  $manifest.layer.library_path=$old
  [IO.File]::WriteAllText((Join-Path $negative 'XPFSR24Layer.json'),($manifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
  $env:VK_LAYER_PATH=$negative
  $env:XPFSR24_LOG=Join-Path $build 'presentation-identity-negative.log'
  $rejected=@(& (Join-Path $build 'present_test.exe') $runtime $build --identity-only 2>&1)
  if($LASTEXITCODE -eq 0 -or -not($rejected|Select-String 'TEST_LAYER_PATH_MISMATCH') -or @($rejected|Where-Object {[string]$_ -eq ('Tested layer='+$old)}).Count -ne 1){throw 'Old renderer negative control did not fail the identity guard'}
  Write-Output 'PASS: deliberately selected old renderer rejected by fixture path guard.'
  $env:VK_LAYER_PATH=Join-Path $app 'layer'
 }
 $correctHash=$env:XP_TEST_LAYER_SHA256
 $env:XP_TEST_LAYER_SHA256=('0'*64)
 $env:XPFSR24_LOG=Join-Path $build 'presentation-hash-negative.log'
 $rejected=@(& (Join-Path $build 'present_test.exe') $runtime $build --identity-only 2>&1)
 if($LASTEXITCODE -eq 0 -or -not($rejected|Select-String 'TEST_LAYER_HASH_MISMATCH')){throw 'Wrong digest negative control did not fail the identity guard'}
 Write-Output 'PASS: candidate with deliberately wrong SHA-256 rejected by fixture hash guard.'
 $env:XP_TEST_LAYER_SHA256=$correctHash
 foreach($presenter in @('3.1.7','4.0.0')){
  $env:XPFSR24_FG_PRESENTER=$presenter
  foreach($profile in 0..2){
   $env:XPFSR24_FG_PACING=[string]$profile
   $env:XPFSR24_LOG=Join-Path $build ('presentation-lifecycle-'+$presenter+'-pacing-'+$profile+'.log')
   if(Test-Path -LiteralPath $env:XPFSR24_LOG){Set-Content -LiteralPath $env:XPFSR24_LOG -Value ''}
   $output=@(& (Join-Path $build 'present_test.exe') $env:XPFSR24_RUNTIME $build)
   $code=$LASTEXITCODE
   $output|Write-Output
   if($code -ne 0){throw 'Layer swapchain create/acquire/present/recreate/retire/idle/destruction check failed'}
   $loaded=@($output | Where-Object {$_ -like 'Tested layer=*'})
   if($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Substring('Tested layer='.Length)).Equals($expectedLayer,[StringComparison]::OrdinalIgnoreCase)){throw 'Presentation fixture loaded a different renderer DLL'}
   if(@($output|Where-Object {$_ -eq ('Tested layer SHA256='+$correctHash)}).Count -ne 1){throw 'Presentation fixture did not attest the freshly built renderer hash'}
   if(-not(Select-String -LiteralPath $env:XPFSR24_LOG -SimpleMatch ('FG SDK presentation proxy ready: provider='+$presenter+' '))){throw 'Fixture bypassed the requested SDK presenter'}
   $profileName=@('Smooth (AMD default)','Recover after heavy scenes','Power saver')[$profile]
   if(-not(Select-String -LiteralPath $env:XPFSR24_LOG -SimpleMatch ('FG pacing profile: '+$profileName+' (applied)'))){throw 'SDK did not accept the selected pacing profile'}
   Write-Output ('PASS: exact candidate layer; presenter '+$presenter+' pacing profile '+$profile+' applied; swapchain lifecycle.')
  }
 }
 Write-Output 'PASS: both FG providers, moving-image GPU readback, actual additional SDK presents, all pacing profiles on both presenters and exact-candidate swapchain lifecycle.'
}finally{
 Get-ChildItem Env:|Where-Object {$_.Name -match $pattern}|ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
 foreach($name in $previous.Keys){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
