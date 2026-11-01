# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/background-files-tests.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $exe -Parent) | Out-Null
& $Compiler -std=c++17 -O2 -static -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/background_files_tests.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'Background file bridge tests did not compile'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Background file bridge tests failed'}
