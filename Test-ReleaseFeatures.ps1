# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$AppDirectory)
$ErrorActionPreference='Stop'
$app=(Resolve-Path -LiteralPath $AppDirectory).Path
$version=(Get-Item -LiteralPath (Join-Path $app 'XPlaneFSR24.exe')).VersionInfo.FileVersion
if($version -notmatch '^0\.7\.1-(alpha|experimental\.1)$'){throw 'Expected a complete 0.7.1 channel build'}
foreach($file in @('FrameGenCheck.exe','runtime/amd_fidelityfx_framegeneration_vk.dll','nr/NR-BUILD.json','nr/XPFSRNeuralRendering.dll','nr/ImportNRModel.exe','nr/NRCheck.exe')){
 if(-not(Test-Path -LiteralPath (Join-Path $app $file))){throw ('Full-feature release component missing: '+$file)}
}
$fixture=Join-Path $PSScriptRoot ('build/release-features-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
foreach($provider in @('3.1.6','4.0.2')){
 $out=Join-Path $fixture $provider
 & (Join-Path $app 'FrameGenCheck.exe') (Join-Path $app 'runtime') $out $provider --srgb --inject-gaps --diagnostics *> ($out+'.txt')
 $code=$LASTEXITCODE
 Get-Content -LiteralPath ($out+'.txt') -Tail 8
 if($code -ne 0){throw ('Released FG color/fallback regression failed: '+$version+' / '+$provider)}
}
Write-Output "PASS: $version contains NR + FG and preserves encoded color/real-frame holds with both providers. Evidence: $fixture"
