# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/fg-pixel-probe-tests.exe'
$inc=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/api/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/upscalers/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/framegeneration/include')))
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX @inc (Join-Path $PSScriptRoot 'src/framegen_pixel_probe_test.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'FG pixel-probe test compilation failed'}
& $exe
if($LASTEXITCODE -ne 0){throw 'FG pixel-probe tests failed'}
