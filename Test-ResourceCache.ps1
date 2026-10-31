# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/resource-cache-tests.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $exe -Parent) | Out-Null
& $Compiler -std=c++17 -O2 -static (Join-Path $PSScriptRoot 'src/resource_cache_tests.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'Completed resource cache tests did not compile'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Completed resource cache tests failed'}
