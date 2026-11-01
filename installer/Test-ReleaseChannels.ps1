# SPDX-License-Identifier: MIT
param(
 [Parameter(Mandatory)][Alias('NormalSetup')][string]$ReleaseSetup,
 [Parameter(Mandatory)][Alias('ExperimentalSetup')][string]$BetaSetup
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
Install-Fixture $ReleaseSetup $true
$settings=Join-Path $installed 'settings.ini'
Add-Content -LiteralPath $settings -Value "`r`n[Updates]`r`nChannel=experimental`r`n[Launcher]`r`nPreset=fsr4-4k-quality`r`nMode=4`r`nFrameGeneration=2`r`nCockpitRestore=0`r`n[FeatureVideo]`r`nAutomatic=0`r`nMuted=1"
$savedHash=(Get-FileHash -LiteralPath $settings).Hash
foreach($transition in @(
 @{setup=$BetaSetup;beta=$true},
 @{setup=$ReleaseSetup;beta=$false},
 @{setup=$BetaSetup;beta=$true},
 @{setup=$ReleaseSetup;beta=$false}
)){
 Install-Fixture $transition.setup
 if((Get-FileHash -LiteralPath $settings).Hash -ne $savedHash){throw 'Channel switch changed user settings'}
 $manifest=Get-Content -LiteralPath (Join-Path $installed 'PAYLOAD-MANIFEST.json') -Raw | ConvertFrom-Json
 if(($manifest.packageVersion -match '-(?:experimental|beta)\.') -ne $transition.beta){throw 'Wrong channel installed'}
 if((Get-Item -LiteralPath (Join-Path $installed 'XPlaneFSR24.exe')).VersionInfo.FileVersion -ne $manifest.packageVersion){throw 'Installed version mismatch'}
 foreach($item in $manifest.files){
  if((Get-FileHash -LiteralPath (Join-Path $installed $item.path)).Hash.ToLowerInvariant() -ne $item.sha256){throw "Channel payload mismatch: $($item.path)"}
 }
 foreach($fgFile in @('FrameGenCheck.exe','runtime\amd_fidelityfx_framegeneration_vk.dll')){
  $fgExpected=$transition.beta -or [version](($manifest.packageVersion -split '-')[0]) -ge [version]'0.7.1'
  if((Test-Path -LiteralPath (Join-Path $installed $fgFile)) -ne $fgExpected){throw "FG payload incorrect: $fgFile"}
 }
 if([version](($manifest.packageVersion -split '-')[0]) -ge [version]'0.7.1'){
  foreach($nrFile in @('nr/NR-BUILD.json','nr/XPFSRNeuralRendering.dll','nr/ImportNRModel.exe')){
   if(-not(Test-Path -LiteralPath (Join-Path $installed $nrFile))){throw "NR payload missing: $nrFile"}
  }
 }
 Write-Output "PASS: channel switch to $($manifest.packageVersion), saved choices preserved, correct FG payload."
}
[ordered]@{status='PASS';releaseSetupSha256=(Get-FileHash -LiteralPath $ReleaseSetup).Hash.ToLowerInvariant();betaSetupSha256=(Get-FileHash -LiteralPath $BetaSetup).Hash.ToLowerInvariant();transitions=4;settingsPreserved='PASS';channelPayloadsVerified='PASS';fixture=$fixture} |
 ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'CHANNEL-SWITCH-RESULTS.json')
