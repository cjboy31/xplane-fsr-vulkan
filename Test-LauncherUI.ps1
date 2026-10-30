# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$Compiler=(Resolve-Path -LiteralPath $Compiler).Path
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
foreach($variant in @('normal','nr')){
 [string[]]$flags=if($variant -eq 'nr'){@('-DXPFSR_FG_DEVELOPMENT=1','-DXPFSR_NR_DEVELOPMENT=1')}else{@()}
 $exe=Join-Path $build ('launcher-ui-tests-'+$variant+'.exe')
 & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @flags (Join-Path $PSScriptRoot 'src\launcher_ui_tests.cpp') '-lcomdlg32' '-lcomctl32' '-lshell32' '-lole32' '-lgdi32' '-lbcrypt' '-lwinhttp' '-lgdiplus' '-ldwmapi' '-luxtheme' '-loleacc' '-luuid' '-lmfuuid' '-loleaut32' '-o' $exe
 if($LASTEXITCODE -ne 0){throw "Main UI test compilation failed: $variant"}
 & $exe (Join-Path $build ('launcher-ui-'+[guid]::NewGuid().ToString('N')))
 if($LASTEXITCODE -ne 0){throw "Main UI regression failed: $variant"}
}
