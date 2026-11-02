param([Parameter(Mandatory)][string]$Compiler,[string]$PreviousPlugin='')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
foreach($name in @('launcher_tests','graphics_tests','session_log_tests')) {
    $exe=Join-Path $build ($name+'.exe')
    [string[]]$entry = @('-municode')
    & $Compiler '-std=c++17' '-O2' '-static' @entry '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $PSScriptRoot ('src\'+$name+'.cpp')) '-lbcrypt' '-o' $exe
    if($LASTEXITCODE -ne 0){throw "Build failed: $name"}
    $fixture=Join-Path $build ($name+'-'+[guid]::NewGuid().ToString('N'))
    & $exe $fixture
    if($LASTEXITCODE -ne 0){throw "Test failed: $name"}
}
if($PreviousPlugin) {
    $exe=Join-Path $build 'launcher_install_tests.exe'
    & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $PSScriptRoot 'src\launcher_install_tests.cpp') '-lcomdlg32' '-lcomctl32' '-lshell32' '-lole32' '-lgdi32' '-lbcrypt' '-lwinhttp' '-lgdiplus' '-ldwmapi' '-luxtheme' '-loleacc' '-luuid' '-lmfuuid' '-loleaut32' '-o' $exe
    if($LASTEXITCODE -ne 0){throw 'Plugin upgrade regression build failed'}
    & $exe (Join-Path $PSScriptRoot 'app') (Resolve-Path -LiteralPath $PreviousPlugin).Path (Join-Path $build ('plugin-upgrade-'+[guid]::NewGuid().ToString('N')))
    if($LASTEXITCODE -ne 0){throw 'Previous-release plugin upgrade failed'}
}
