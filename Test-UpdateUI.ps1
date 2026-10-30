# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
foreach($variant in @('normal','experimental')){
 $exe=Join-Path $build ('update_ui_tests-'+$variant+'.exe')
 [string[]]$flags=if($variant -eq 'experimental'){@('-DXPFSR_FG_DEVELOPMENT=1')}else{@()}
 & $Compiler '-std=c++17' '-O2' '-static' '-municode' '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' @flags (Join-Path $PSScriptRoot 'src\update_ui_tests.cpp') '-lcomctl32' '-lshell32' '-lole32' '-lgdi32' '-lbcrypt' '-lwinhttp' '-lgdiplus' '-ldwmapi' '-luxtheme' '-loleacc' '-luuid' '-lmfuuid' '-loleaut32' '-o' $exe
 if($LASTEXITCODE -ne 0){throw "Updater UI test compilation failed: $variant"}
 & $exe (Join-Path $build ('update-ui-'+[guid]::NewGuid().ToString('N')))
 if($LASTEXITCODE -ne 0){throw "Updater UI regression failed: $variant"}
}
