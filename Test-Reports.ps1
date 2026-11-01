param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'report-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/report_tests.cpp') -lbcrypt -ladvapi32 -lwinhttp -lcrypt32 -luser32 -lshell32 -lgdi32 -o $exe
if($LASTEXITCODE -ne 0){throw 'Report support tests did not compile'}
& $exe (Join-Path $build ('report-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Report support tests failed'}
