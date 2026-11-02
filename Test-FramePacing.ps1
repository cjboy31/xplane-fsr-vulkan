# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'frame-pacing-tests.exe'
& $Compiler -std=c++17 -O2 -static (Join-Path $PSScriptRoot 'src/frame_pacing_tests.cpp') -o $exe
if($LASTEXITCODE -ne 0){throw 'Frame pacing tests did not compile'}
& $exe
if($LASTEXITCODE -ne 0){throw 'Frame pacing tests failed'}
