# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler,[Parameter(Mandatory)][string]$VulkanHeaders)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'build') | Out-Null
foreach($variant in @('normal','nr')){
 [string[]]$flags=if($variant -eq 'nr'){@('-DXPFSR_NR_DEVELOPMENT=1')}else{@()}
 $exe=Join-Path $PSScriptRoot ('build\nr-tests-'+$variant+'.exe')
 & $Compiler '-std=c++17' '-O2' '-static' ('-I'+(Join-Path $VulkanHeaders 'include')) @flags (Join-Path $PSScriptRoot 'src\nr_tests.cpp') '-o' $exe
 if($LASTEXITCODE -ne 0){throw 'NR regression build failed'}
 & $exe
 if($LASTEXITCODE -ne 0){throw 'NR regression failed'}
}
