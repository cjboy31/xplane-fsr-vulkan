# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'build/session-export-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/session_export_tests.cpp') -lbcrypt -o $exe
if($LASTEXITCODE -ne 0){throw 'Session export tests did not compile'}
& $exe (Join-Path $PSScriptRoot ('build/session-export-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Session export tests failed'}
