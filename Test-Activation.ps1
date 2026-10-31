# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'activation-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/activation_tests.cpp') -lbcrypt -ladvapi32 -o $exe
if($LASTEXITCODE -ne 0){throw 'Activation tests did not compile'}
& $exe (Join-Path $build ('activation-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Activation tests failed'}
