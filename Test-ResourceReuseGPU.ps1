# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$VulkanHeaders,[Parameter(Mandatory)][string]$FsrSdk)
$ErrorActionPreference='Stop'
$includes=@(('-I'+(Join-Path $PSScriptRoot 'build/generated')),('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/api/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/upscalers/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/framegeneration/include')))
$exe=Join-Path $PSScriptRoot 'build/resource-reuse-gpu.exe'
& $Compiler -std=c++17 -O2 -static -municode '-Wl,--no-insert-timestamp' -DWIN32_LEAN_AND_MEAN -DNOMINMAX @includes (Join-Path $PSScriptRoot 'src/resource_reuse_gpu.cpp') -lbcrypt -ladvapi32 -o $exe
if($LASTEXITCODE -ne 0){throw 'Production resource GPU fixture did not compile'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Production resource GPU fixture failed'}
