# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
foreach($name in @('gpu_frame_queue','gpu_selection','gpu_identity')){
 $exe=Join-Path $PSScriptRoot ('build/'+$name+'-tests.exe')
 New-Item -ItemType Directory -Force -Path (Split-Path $exe -Parent)|Out-Null
 & $Compiler -std=c++17 -O2 -static -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot ('src/'+$name+'_tests.cpp')) -o $exe
 if($LASTEXITCODE -ne 0){throw "GPU infrastructure test compilation failed: $name"}
 & $exe
 if($LASTEXITCODE -ne 0){throw "GPU infrastructure test failed: $name"}
}
