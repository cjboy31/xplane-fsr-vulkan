param([Parameter(Mandatory)][string]$Compiler, [switch]$Live, [string]$DownloadTo='')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe=Join-Path $build 'update_tests.exe'
& $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $PSScriptRoot 'src\update_tests.cpp') '-lwinhttp' '-lbcrypt' '-o' $exe
if($LASTEXITCODE -ne 0){throw 'Updater test compilation failed'}
$arguments=@()
if($Live){$arguments+='--live';if($DownloadTo){$arguments+=$DownloadTo}}
& $exe @arguments
if($LASTEXITCODE -ne 0){throw 'Updater checks failed'}
