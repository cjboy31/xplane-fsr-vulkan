# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'settings-transaction-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/settings_transaction_tests.cpp') -lbcrypt -o $exe
if($LASTEXITCODE -ne 0){throw 'Settings transaction tests did not compile'}
& $exe (Join-Path $build ('settings-transaction-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Settings transaction tests failed'}
