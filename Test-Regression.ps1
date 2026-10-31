# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference='Stop'
$cc=(Resolve-Path -LiteralPath $Compiler).Path
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$checks=[Collections.Generic.List[string]]::new()
$core=Join-Path $build 'core_tests.exe'
& $cc -std=c++17 -O2 -static -DWIN32_LEAN_AND_MEAN -DNOMINMAX (Join-Path $PSScriptRoot 'src/core_tests.cpp') -o $core
if($LASTEXITCODE -ne 0){throw 'Core regression compilation failed'}
& $core
if($LASTEXITCODE -ne 0){throw 'Core regression failed'}
$checks.Add('Camera/frame continuity, startup readiness and FG callback state')
foreach($script in @('Test-Launcher.ps1','Test-SettingsTransaction.ps1','Test-InSimPanel.ps1','Test-NRModelImport.ps1','Test-NRPassPolicy.ps1','Test-NRMemory.ps1','Test-Activation.ps1','Test-DeferredGraphics.ps1','Test-Updater.ps1','Test-UpdateUI.ps1','Test-UpdateFlow.ps1','Test-LauncherUI.ps1')){
 & (Join-Path $PSScriptRoot $script) -Compiler $cc
 if(-not $?){throw "Regression failed: $script"}
 $checks.Add($script)
}
& (Join-Path $PSScriptRoot 'Test-Updater.ps1') -Compiler $cc -Experimental
if(-not $?){throw 'Experimental updater regression failed'}
$checks.Add('Test-Updater.ps1 -Experimental')
[ordered]@{status='PASS';checks=@($checks);scope='CPU and native UI fixtures; no desktop input, simulator launch, network download or GPU quality claim'} |
 ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'REGRESSION-RESULTS.json') -Encoding utf8
Write-Output 'PASS: CPU, preferences/logs, both updater channels, update handoff and native UI regression suite.'
