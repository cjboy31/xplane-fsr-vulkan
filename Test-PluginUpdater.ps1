param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$exe=Join-Path $build 'plugin-updater-tests.exe'
& $Compiler -std=c++17 -O2 -static -municode -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/plugin_updater_tests.cpp') -lgdi32 -lcomctl32 -lshell32 -lole32 -lbcrypt -lwinhttp -lgdiplus -ldwmapi -luxtheme -loleacc -luuid -o $exe
if($LASTEXITCODE -ne 0){throw 'Update-only native fixture compilation failed'}
& $exe (Join-Path $build ('plugin-updater-fixture-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Update-only fixture failed'}
$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$managed=Join-Path $build 'native-aa-updater-tests.exe'
& $csc /nologo /target:exe /main:NativeAAUpdaterTests /platform:x64 /optimize+ /warnaserror+ /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ('/out:'+$managed) (Join-Path $PSScriptRoot 'tools/NativeAAUpdater.cs') (Join-Path $PSScriptRoot 'tools/NativeAAUpdaterTests.cs')
if($LASTEXITCODE -ne 0){throw 'Native AA update policy compilation failed'}
& $managed (Join-Path $build ('native-aa-updater-fixture-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Native AA update policy failed'}
