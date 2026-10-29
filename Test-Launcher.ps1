param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
foreach($name in @('launcher_tests','graphics_tests')) {
    $exe=Join-Path $build ($name+'.exe')
    & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' (Join-Path $PSScriptRoot ('src\'+$name+'.cpp')) '-o' $exe
    if($LASTEXITCODE -ne 0){throw "Build failed: $name"}
    $fixture=Join-Path $build ($name+'-'+[guid]::NewGuid().ToString('N'))
    & $exe $fixture
    if($LASTEXITCODE -ne 0){throw "Test failed: $name"}
}
