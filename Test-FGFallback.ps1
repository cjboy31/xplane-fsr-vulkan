# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$VulkanHeaders,
 [Parameter(Mandatory)][string]$FsrSdk)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/fg-callback-tests.exe'
$inc=@(('-I'+(Join-Path $VulkanHeaders 'include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/api/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/upscalers/include')),('-I'+(Join-Path $FsrSdk 'Kits/FidelityFX/framegeneration/include')))
& $Compiler -std=c++17 -O2 -static -DWIN32_LEAN_AND_MEAN -DNOMINMAX @inc (Join-Path $PSScriptRoot 'src/framegen_callback_tests.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'FG callback fault-test compilation failed'}
& $exe
if($LASTEXITCODE -ne 0){throw 'FG callback fault tests failed'}
