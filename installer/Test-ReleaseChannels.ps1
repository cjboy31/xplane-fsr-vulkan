# SPDX-License-Identifier: MIT
param(
 [Parameter(Mandatory)][string]$NormalSetup,
 [Parameter(Mandatory)][string]$ExperimentalSetup
)
$ErrorActionPreference='Stop'
$fixture=Join-Path $PSScriptRoot ('test-channels-'+[guid]::NewGuid().ToString('N'))
$installed=Join-Path $fixture 'installed app'
$simulator=Join-Path $fixture 'simulator fixture'
New-Item -ItemType Directory -Path $simulator | Out-Null
[IO.File]::WriteAllText((Join-Path $simulator 'X-Plane.exe'),'Inert installer fixture; never executed.')
function Install-Fixture([string]$Setup,[bool]$First=$false){
 $arguments=if($First){'/S /TEST=1 /XPLANE="'+$simulator+'" /D='+$installed}else{'/S /TEST=1 /AUTOUPDATE=1 /D='+$installed}
 $p=Start-Process -FilePath $Setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
 if($p.ExitCode -ne 0){throw "Channel installation failed: $($p.ExitCode)"}
}
Install-Fixture $NormalSetup $true
$settings=Join-Path $installed 'settings.ini'
Add-Content -LiteralPath $settings -Value "`r`n[Updates]`r`nChannel=experimental`r`n[Launcher]`r`nPreset=fsr4-4k-quality`r`nMode=4`r`nFrameGeneration=2`r`nCockpitRestore=0`r`n[FeatureVideo]`r`nAutomatic=0`r`nMuted=1"
$savedHash=(Get-FileHash -LiteralPath $settings).Hash
foreach($transition in @(
 @{setup=$ExperimentalSetup;experimental=$true},
 @{setup=$NormalSetup;experimental=$false},
 @{setup=$ExperimentalSetup;experimental=$true},
 @{setup=$NormalSetup;experimental=$false}
)){
 Install-Fixture $transition.setup
 if((Get-FileHash -LiteralPath $settings).Hash -ne $savedHash){throw 'Channel switch changed user settings'}
 $manifest=Get-Content -LiteralPath (Join-Path $installed 'PAYLOAD-MANIFEST.json') -Raw | ConvertFrom-Json
 if(($manifest.packageVersion -match '-experimental\.') -ne $transition.experimental){throw 'Wrong channel installed'}
 if((Get-Item -LiteralPath (Join-Path $installed 'XPlaneFSR24.exe')).VersionInfo.FileVersion -ne $manifest.packageVersion){throw 'Installed version mismatch'}
 foreach($item in $manifest.files){
  if((Get-FileHash -LiteralPath (Join-Path $installed $item.path)).Hash.ToLowerInvariant() -ne $item.sha256){throw "Channel payload mismatch: $($item.path)"}
 }
 foreach($fgFile in @('FrameGenCheck.exe','runtime\amd_fidelityfx_framegeneration_vk.dll')){
  $fgExpected=$transition.experimental -or [version](($manifest.packageVersion -split '-')[0]) -ge [version]'0.7.1'
  if((Test-Path -LiteralPath (Join-Path $installed $fgFile)) -ne $fgExpected){throw "FG payload incorrect: $fgFile"}
 }
 if([version](($manifest.packageVersion -split '-')[0]) -ge [version]'0.7.1'){
  foreach($nrFile in @('nr/NR-BUILD.json','nr/XPFSRNeuralRendering.dll','nr/ImportNRModel.exe')){
   if(-not(Test-Path -LiteralPath (Join-Path $installed $nrFile))){throw "NR payload missing: $nrFile"}
  }
 }
 Write-Output "PASS: channel switch to $($manifest.packageVersion), saved choices preserved, correct FG payload."
}
[ordered]@{status='PASS';normalSetupSha256=(Get-FileHash -LiteralPath $NormalSetup).Hash.ToLowerInvariant();experimentalSetupSha256=(Get-FileHash -LiteralPath $ExperimentalSetup).Hash.ToLowerInvariant();transitions=4;settingsPreserved='PASS';channelPayloadsVerified='PASS';fixture=$fixture} |
 ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'CHANNEL-SWITCH-RESULTS.json')
