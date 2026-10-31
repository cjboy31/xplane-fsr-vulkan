# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/nr-memory-tests.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $exe -Parent) | Out-Null
& $Compiler -std=c++17 -O2 -static -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/nr_memory_tests.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'NR memory regression compilation failed'}
& $exe
if($LASTEXITCODE -ne 0){throw 'NR memory regression failed'}
