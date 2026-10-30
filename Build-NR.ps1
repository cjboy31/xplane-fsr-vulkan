param(
 [Parameter(Mandatory)][string]$Compiler,
 [Parameter(Mandatory)][string]$Glslang,
 [Parameter(Mandatory)][string]$Python,
 [Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$Reference,
 [switch]$SkipShaders,
 [string]$AppDirectory=''
)
$ErrorActionPreference='Stop'
$pin='960038fd1cb892742548f408355e0f3b2fd30d6f'
$ref=(Resolve-Path -LiteralPath $Reference).Path
if((& git -C $ref rev-parse HEAD) -ne $pin){throw 'NR reference must match the reviewed pinned MIT source commit'}
if((& git -C $ref status --porcelain --untracked-files=no)){throw 'NR reference source has tracked changes'}
$cc=(Resolve-Path -LiteralPath $Compiler).Path
$headers=(Resolve-Path -LiteralPath $VulkanHeaders).Path
$app=if($AppDirectory){[IO.Path]::GetFullPath($AppDirectory)}else{Join-Path $PSScriptRoot 'app'}
$out=Join-Path $app 'nr'
$data=Join-Path $out 'dlssnr-amd'
$generated=Join-Path $PSScriptRoot 'build\nr'
$shaderRecord=Join-Path $generated 'SHADERS.json'
New-Item -ItemType Directory -Force -Path $out,$data,$generated,(Join-Path $ref 'toolchain\glslang\bin') | Out-Null
Copy-Item -LiteralPath $Glslang -Destination (Join-Path $ref 'toolchain\glslang\bin\glslang.exe') -Force
if(-not $SkipShaders){
 $shaders=[IO.Path]::GetFullPath((Join-Path $data 'shaders'))
 $workspace=[IO.Path]::GetFullPath($PSScriptRoot)+[IO.Path]::DirectorySeparatorChar
 if(-not $shaders.StartsWith($workspace,[StringComparison]::OrdinalIgnoreCase)){throw 'NR shader target escaped the workspace'}
 # Upstream recreates exactly this verified generated shader directory. Models
 # live beside it, are never supplied to the build, and are not touched here.
 & $Python (Join-Path $ref 'windows\build\build_network.py') rdna4 --out $shaders
 if($LASTEXITCODE -ne 0){throw 'NR shader build failed'}
 $shaderFiles=@(Get-ChildItem -LiteralPath $shaders -File -Recurse | Sort-Object FullName | ForEach-Object {
  [ordered]@{path=[IO.Path]::GetRelativePath($data,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
 })
 [IO.File]::WriteAllText($shaderRecord,([ordered]@{sourceCommit=$pin;files=$shaderFiles}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
}else{
 $record=Get-Content -LiteralPath $shaderRecord -Raw | ConvertFrom-Json
 if($record.sourceCommit -ne $pin -or $record.files.Count -lt 70){throw 'Missing or unreviewed shader provenance; rebuild without SkipShaders'}
 foreach($entry in $record.files){
  if($entry.path -notmatch '^shaders/(?:[a-z0-9_-]+/)?[a-z0-9_-]+\.(spv|txt)$' -or
     (Get-FileHash -LiteralPath (Join-Path $data $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256){throw 'NR shader integrity mismatch; rebuild without SkipShaders'}
 }
}
$toolbin=Split-Path $cc -Parent
$exports=& (Join-Path $toolbin 'llvm-readobj.exe') --coff-exports (Join-Path $env:WINDIR 'System32\vulkan-1.dll')
$names=@($exports|ForEach-Object {if($_ -match '^\s*Name: (vk\w+)$'){$matches[1]}})
if($names.Count -lt 100){throw 'Cannot enumerate the installed Vulkan loader exports'}
$def=Join-Path $generated 'vulkan-loader.def'
[IO.File]::WriteAllLines($def,(@('LIBRARY vulkan-1.dll','EXPORTS')+$names),[Text.UTF8Encoding]::new($false))
$import=Join-Path $generated 'vulkan-loader.lib'
& (Join-Path $toolbin 'llvm-dlltool.exe') -m i386:x86-64 -d $def -l $import
if($LASTEXITCODE -ne 0){throw 'Vulkan import library generation failed'}
$defines=@('-DNR_GEMM_WIDE_MT=64','-DNR_GEMM_WIDE_NT=256','-DNR_GEMM_PROJ_MT=32','-DNR_GEMM_PROJ_NT=128',
 '-DNR_PROJW_MIN_TOKENS=768','-DNR_GEMM_REMAP_HOST=1','-DNR_FFWD_WGW=4','-DNR_FFWD_GMAJOR=1','-DNR_UPSVIEW_VEC=1','-DNR_REPACK_VEC=1',
 '-DNR_WIDE_UPS_MASK=7','-DNR_PERSIST_DF=1','-DNR_DS_FUSE=15','-DNR_DECUPS_VEC=16','-DNR_DECQ_SMALL=1',
 '-DNR_PERSIST_DS_MASK=7','-DNR_PERSIST_UPS_MASK=7','-DNR_REPACK_FOLD=1','-DNR_STRAG_PCT=25',
 '-DNR_PERSIST_ONE_MASK=6','-DNR_NOISE_FIELD=1','-DNR_FFWD_FM2_MIN_TOKENS=2560','-DNR_POST_ALPHA=1','-DNR_TCHAIN_TILES=1')
$core=Join-Path $ref 'windows\src\core'
$flags=@('-std=c++17','-O2','-static','-DNDEBUG','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX',('-I'+(Join-Path $headers 'include')),('-I'+$core))
& $cc @flags @defines -shared '-Wl,--no-insert-timestamp' (Join-Path $PSScriptRoot 'src\nr_bridge.cpp') (Join-Path $core 'nr_runtime.cpp') (Join-Path $core 'nr_native_plan.cpp') $import -ldxgi -lole32 -lbcrypt -o (Join-Path $out 'XPFSRNeuralRendering.dll')
if($LASTEXITCODE -ne 0){throw 'NR bridge build failed'}
& $cc @flags -municode '-Wl,--no-insert-timestamp' (Join-Path $ref 'windows\package\model-tools\dlssnr_extract_model.cpp') -o (Join-Path $out 'ImportNRModel.exe')
if($LASTEXITCODE -ne 0){throw 'NR model importer build failed'}
& $cc @flags -municode '-Wl,--no-insert-timestamp' (Join-Path $PSScriptRoot 'src\nr_check.cpp') $import -ldxgi -lole32 -lbcrypt -o (Join-Path $out 'NRCheck.exe')
if($LASTEXITCODE -ne 0){throw 'NR GPU check build failed'}
New-Item -ItemType Directory -Force -Path (Join-Path $app 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $ref 'LICENSE') -Destination (Join-Path $app 'licenses\DLSSNR-AMD-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $ref 'THIRD_PARTY.md') -Destination (Join-Path $app 'licenses\DLSSNR-AMD-UPSTREAM-NOTICES.md') -Force
Copy-Item -LiteralPath (Join-Path $ref 'windows\shaders\passes\include\fsr1\LICENSE.txt') -Destination (Join-Path $app 'licenses\FidelityFX-FSR1-MIT.txt') -Force
$owned=@('XPFSRNeuralRendering.dll','ImportNRModel.exe','NRCheck.exe')+@($shaderFiles | ForEach-Object {'dlssnr-amd/'+$_.path})
if($SkipShaders){$owned=@('XPFSRNeuralRendering.dll','ImportNRModel.exe','NRCheck.exe')+@($record.files | ForEach-Object {'dlssnr-amd/'+$_.path})}
$payload=@($owned | ForEach-Object {
 $file=Join-Path $out $_
 [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $file).Length}
})
[IO.File]::WriteAllText((Join-Path $out 'NR-BUILD.json'),([ordered]@{component='XPFSR independent neural rendering';sourceRepository='mochizuki0323/DLSSNR-AMD';sourceCommit=$pin;modelsIncluded=$false;acoIncluded=$false;files=$payload}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output 'NR backend and importer built from pinned MIT source. No model DLL or weights were copied.'
