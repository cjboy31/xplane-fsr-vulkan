# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$VulkanHeaders,[string]$FsrSdk='',[string]$XessSdk='')
$ErrorActionPreference='Stop'
$cc=(Resolve-Path -LiteralPath $Compiler).Path
$headers=(Resolve-Path -LiteralPath $VulkanHeaders).Path
$out=Join-Path $PSScriptRoot 'build/multi-gpu'
New-Item -ItemType Directory -Force -Path $out|Out-Null
$bin=Split-Path $cc -Parent
$exports=& (Join-Path $bin 'llvm-readobj.exe') --coff-exports (Join-Path $env:WINDIR 'System32/vulkan-1.dll')
$names=@($exports|ForEach-Object {if($_ -match '^\s*Name: (vk\w+)$'){$matches[1]}})
if($names.Count -lt 100){throw 'Installed Vulkan loader exports unavailable'}
$def=Join-Path $out 'vulkan-loader.def';$lib=Join-Path $out 'vulkan-loader.lib'
[IO.File]::WriteAllLines($def,(@('LIBRARY vulkan-1.dll','EXPORTS')+$names),[Text.UTF8Encoding]::new($false))
& (Join-Path $bin 'llvm-dlltool.exe') -m i386:x86-64 -d $def -l $lib
if($LASTEXITCODE -ne 0){throw 'Vulkan import library failed'}
$flags=@('-std=c++17','-O2','-static','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX',('-I'+(Join-Path $headers 'include')),('-I'+(Join-Path $PSScriptRoot 'src')))
foreach($tool in @(@('gpu_inventory.cpp','GpuInventory.exe'),@('multi_gpu_transport.cpp','MultiGpuTransportCheck.exe'))){
 & $cc @flags (Join-Path $PSScriptRoot ('tools/'+$tool[0])) $lib -o (Join-Path $out $tool[1])
 if($LASTEXITCODE -ne 0){throw "GPU probe failed: $($tool[0])"}
}
Write-Output 'Built experimental adapter/transport probes. No simulator activation, installer or GPU setting was changed.'
if($FsrSdk){
 $fsr=(Resolve-Path -LiteralPath $FsrSdk).Path
 $includes=@(('-I'+(Join-Path $fsr 'Kits/FidelityFX/api/include')),('-I'+(Join-Path $fsr 'Kits/FidelityFX/upscalers/include')),('-I'+(Join-Path $fsr 'Kits/FidelityFX/framegeneration/include')))
 & $cc @flags @includes -municode (Join-Path $PSScriptRoot 'tools/multi_gpu_framegen.cpp') $lib -lbcrypt -o (Join-Path $out 'MultiGpuFrameGenCheck.exe')
 if($LASTEXITCODE -ne 0){throw 'Compute-only FG worker fixture failed to compile'}
}
if($XessSdk){
 $intel=(Resolve-Path -LiteralPath $XessSdk).Path
 & $cc @flags ('-I'+(Join-Path $intel 'inc')) -municode (Join-Path $PSScriptRoot 'tools/xess_vk_check.cpp') $lib -lbcrypt -o (Join-Path $out 'XeSSVulkanCheck.exe')
 if($LASTEXITCODE -ne 0){throw 'Intel Vulkan fixture failed to compile'}
}
