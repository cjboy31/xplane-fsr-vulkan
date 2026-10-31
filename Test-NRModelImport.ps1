# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[string]$SourceDll='',[string]$Importer='')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'nr-model-import-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/nr_model_import_tests.cpp') -lbcrypt -o $exe
if($LASTEXITCODE -ne 0){throw 'NR model import tests did not compile'}
$fixture=Join-Path $build ('nr-model-import-'+[guid]::NewGuid().ToString('N'))
if($SourceDll){
 if(-not $Importer){throw 'Supply the pinned ImportNRModel.exe for a local model test'}
 $extracted=Join-Path $build ('nr-model-extracted-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $extracted | Out-Null
 & $Importer $SourceDll $extracted
 if($LASTEXITCODE -ne 0){throw 'Local DLL extraction failed'}
 & $exe $fixture $extracted
}else{& $exe $fixture}
if($LASTEXITCODE -ne 0){throw 'NR model import tests failed'}
