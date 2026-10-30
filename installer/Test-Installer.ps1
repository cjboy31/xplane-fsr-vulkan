param([string]$WorkspaceRoot=(Split-Path $PSScriptRoot -Parent),[string]$ArchiveName='XPlane-FSR24-Installer.rar',[string]$PackageFolder='XPlane-FSR24-Installation',[string]$ResultName='INSTALLER-TEST-RESULTS.json')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$testRoot = Join-Path $PSScriptRoot ('test-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$extracted = Join-Path $testRoot 'extracted'
$installed = Join-Path $testRoot 'installed app'
$unrelated = Join-Path $testRoot 'unrelated'
$invalid = Join-Path $testRoot 'invalid-target'
$rar = 'C:\Program Files\WinRAR\Rar.exe'
$archive = Join-Path (Join-Path $root 'outputs') $ArchiveName
New-Item -ItemType Directory -Force -Path $testRoot,$extracted,$unrelated | Out-Null
$bytes = [IO.File]::ReadAllBytes($archive)
if([BitConverter]::ToString($bytes[0..7]) -ne '52-61-72-21-1A-07-01-00'){throw 'Not a genuine RAR5 archive'}
& $rar t '-idq' $archive
if($LASTEXITCODE -ne 0){throw 'Archive integrity failed'}
& $rar x '-idq' '-o-' $archive ($extracted+'\')
if($LASTEXITCODE -ne 0){throw 'Archive extraction failed'}
$package = Join-Path $extracted $PackageFolder
foreach($line in (Get-Content -LiteralPath (Join-Path $package 'PACKAGE-SHA256.txt'))){
  $match = [regex]::Match($line,'^([a-f0-9]{64})  (.+)$')
  if(-not $match.Success){throw 'Malformed outer manifest'}
  $path = Join-Path $package $match.Groups[2].Value
  if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $match.Groups[1].Value){throw "Outer hash mismatch: $path"}
}
$setup = Join-Path $package 'XPlane-FSR24-Setup.exe'
function Run-Setup([string]$ArgumentString){
  $process = Start-Process -FilePath $setup -ArgumentList $ArgumentString -WindowStyle Hidden -PassThru -Wait
  return $process.ExitCode
}
Set-Content -LiteralPath (Join-Path $unrelated 'keep.txt') -Value 'Do not overwrite this unrelated folder.'
$code = Run-Setup ('/S /TEST=1 /XPLANE= /D='+$unrelated)
if($code -ne 4){throw "Unrelated folder was not rejected with code 4: $code"}
if(@(Get-ChildItem -LiteralPath $unrelated).Count -ne 1){throw 'Unrelated directory changed'}
$code = Run-Setup ('/S /TEST=1 /XPLANE="'+(Join-Path $testRoot 'no-simulator')+'" /D='+$invalid)
if($code -ne 3){throw "Invalid simulator path was not rejected with code 3: $code"}
if(Test-Path -LiteralPath $invalid){throw 'Invalid-path installation created files'}
$simulator = Join-Path $testRoot 'simulator fixture'
$cameraFolder = Join-Path $simulator 'Resources\plugins\XPFSR24\64'
$prefsFolder = Join-Path $simulator 'Output\preferences'
New-Item -ItemType Directory -Force -Path $cameraFolder,$prefsFolder | Out-Null
# Setup validates the filename only. These inert sentinels are never executed.
Set-Content -LiteralPath (Join-Path $simulator 'X-Plane.exe') -Value 'Installer fixture only; not executable.'
Set-Content -LiteralPath (Join-Path $cameraFolder 'win.xpl') -Value 'Preserve this fixture camera plugin.'
Set-Content -LiteralPath (Join-Path $prefsFolder 'X-Plane.prf') -Value 'Preserve these fixture graphics preferences.'
$camera = Join-Path $simulator 'Resources\plugins\XPFSR24\64\win.xpl'
$cameraBefore = if(Test-Path -LiteralPath $camera){(Get-FileHash -LiteralPath $camera).Hash}else{''}
$prefs = Join-Path $simulator 'Output\preferences\X-Plane.prf'
$prefsBefore = if(Test-Path -LiteralPath $prefs){(Get-FileHash -LiteralPath $prefs).Hash}else{''}
$code = Run-Setup ('/S /TEST=1 /XPLANE="'+$simulator+'" /D='+$installed)
if($code -ne 0){throw "Installation failed: $code"}
$manifest = Get-Content -LiteralPath (Join-Path $installed 'PAYLOAD-MANIFEST.json') -Raw | ConvertFrom-Json
$nrPackage='nr/XPFSRNeuralRendering.dll' -in $manifest.files.path
if($nrPackage){
  foreach($excluded in @('nvngx_dlssnr.dll','dlssnr-small.bin','dlssnr.bin')){
    if(@($manifest.files.path | Where-Object {($_ -split '/')[-1] -eq $excluded}).Count){throw 'Private NR asset included in payload'}
  }
}
foreach($required in @('XPlaneFSR24.exe','NativeAACheck.exe','CockpitRestoreCheck.exe','layer/XPFSR24Layer.dll','plugin/64/win.xpl')){
  if($required -notin $manifest.files.path){throw "Required app component absent from installer: $required"}
}
foreach($excluded in @('FrameGenCheck.exe','runtime/amd_fidelityfx_framegeneration_vk.dll')){
 $expectedFG = $manifest.packageVersion -match '^\d+\.\d+\.\d+-experimental\.\d+$'
 if($expectedFG){
  if($excluded -notin $manifest.files.path -or -not(Test-Path -LiteralPath (Join-Path $installed $excluded))){throw ('Experimental FG component is missing: '+$excluded)}
 }elseif($excluded -in $manifest.files.path -or (Test-Path -LiteralPath (Join-Path $installed $excluded))){throw ('Suspended FG component was packaged: '+$excluded)}
}
foreach($file in $manifest.files){
  $path = Join-Path $installed $file.path
  if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $file.sha256){throw "Installed hash mismatch: $($file.path)"}
}
$featureVideo = $null
if([version](($manifest.packageVersion -split '-')[0]) -ge [version]'0.6.2') {
  $videoVersion = $manifest.packageVersion
  foreach($name in @(($videoVersion+'.mp4'),'feature-video.ini','TRANSCRIPT.txt','CAPTIONS.srt','PROVENANCE.json','CREDITS-AND-LICENSES.txt')) {
    if(('media/'+$name) -notin $manifest.files.path){throw ('Required feature guide file absent: '+$name)}
  }
  $videoFile = Join-Path $installed ('media\'+$videoVersion+'.mp4')
  $featureVideo = @{version=$videoVersion;sha256=(Get-FileHash -LiteralPath $videoFile).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $videoFile).Length}
  $videoManifest = [IO.File]::ReadAllText((Join-Path $installed 'media\feature-video.ini'))
  foreach($line in @("Version=$($featureVideo.version)","SHA256=$($featureVideo.sha256)","Bytes=$($featureVideo.bytes)")) {
    if($videoManifest -notmatch ('(?m)^'+[regex]::Escape($line)+'\r?$')){throw 'Installed feature-video manifest does not match payload'}
  }
}
if((Get-Content -LiteralPath (Join-Path $installed 'settings.ini') -Raw) -notmatch [regex]::Escape('Simulator='+$simulator)){throw 'Simulator path was not saved'}
# Verify a recognized update keeps local data and still produces identical files.
New-Item -ItemType Directory -Path (Join-Path $installed 'logs'),(Join-Path $installed 'plugin-backups') | Out-Null
Set-Content -LiteralPath (Join-Path $installed 'logs\keep.txt') -Value 'Test user log'
Set-Content -LiteralPath (Join-Path $installed 'plugin-backups\keep.txt') -Value 'Test user backup'
Set-Content -LiteralPath (Join-Path $installed 'user-file.txt') -Value 'Test unrelated user file'
$nrUserFiles=@()
if($nrPackage){
  $nrUserFiles=@('nr\dlssnr-amd\dlssnr-small.bin','nr\dlssnr-amd\cache\user-cache.bin')
  New-Item -ItemType Directory -Force -Path (Join-Path $installed 'nr\dlssnr-amd\cache') | Out-Null
  foreach($relative in $nrUserFiles){Set-Content -LiteralPath (Join-Path $installed $relative) -Value 'Inert local-model/cache preservation sentinel; never executed'}
}
Add-Content -LiteralPath (Join-Path $installed 'settings.ini') -Value "`r`n[Launcher]`r`nPreset=fsr4-4k-quality`r`nMode=4`r`nFrameGeneration=2`r`nCockpitRestore=0`r`nEnabled=1`r`nGhostReduction=0`r`nAlignmentTint=0`r`n[Window]`r`nClientWidth=900`r`nClientHeight=740"
$savedChoiceBlock = (Get-Content -LiteralPath (Join-Path $installed 'settings.ini') -Raw) -split '\[Launcher\]',2 | Select-Object -Last 1
$code = Run-Setup ('/S /TEST=1 /XPLANE="'+$simulator+'" /D='+$installed)
if($code -ne 0){throw "Recognized update failed: $code"}
$updatedChoiceBlock = (Get-Content -LiteralPath (Join-Path $installed 'settings.ini') -Raw) -split '\[Launcher\]',2 | Select-Object -Last 1
if($savedChoiceBlock.Trim() -ne $updatedChoiceBlock.Trim()){throw 'Recognized update changed saved launcher choices'}
foreach($file in $manifest.files){if((Get-FileHash -LiteralPath (Join-Path $installed $file.path)).Hash.ToLowerInvariant() -ne $file.sha256){throw "Update hash mismatch: $($file.path)"}}
# Automatic update requires an owned install and preserves the whole settings file.
$code = Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /D='+$unrelated)
if($code -ne 5){throw "Automatic update accepted an unrelated target: $code"}
$settings = Join-Path $installed 'settings.ini'
Add-Content -LiteralPath $settings -Value "`r`n[Updates]`r`nAutomaticChecks=0`r`nPreviewReleases=0`r`nChannel=experimental"
Add-Content -LiteralPath $settings -Value "`r`n[FeatureVideo]`r`nAutomatic=0`r`nMuted=1`r`nLastPresented=0.6.1-alpha"
$settingsHash=(Get-FileHash -LiteralPath $settings).Hash
$lockPath=Join-Path $installed 'launcher.lock'
$heldLock=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
  $code = Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /D='+$installed)
  if($code -ne 6){throw "Automatic update did not refuse a running launcher: $code"}
} finally { $heldLock.Dispose(); Remove-Item -LiteralPath $lockPath -ErrorAction SilentlyContinue }
$heldPayload=[IO.File]::Open((Join-Path $installed 'NativeAACheck.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
  $code=Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /D='+$installed)
  if($code -ne 7){throw "Automatic update did not refuse a loaded/locked payload: $code"}
} finally { $heldPayload.Dispose() }
if($nrPackage){
  $heldNR=[IO.File]::Open((Join-Path $installed 'nr\XPFSRNeuralRendering.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try{
    $code=Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /D='+$installed)
    if($code -ne 7){throw "Automatic update did not refuse a locked NR backend: $code"}
  }finally{$heldNR.Dispose()}
  foreach($relative in $nrUserFiles){if((Get-Content -LiteralPath (Join-Path $installed $relative) -Raw).Trim() -ne 'Inert local-model/cache preservation sentinel; never executed'){throw 'Update altered a local NR model/cache file'}}
}
$parent = Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoProfile','-Command','Start-Sleep -Milliseconds 1800') -WindowStyle Hidden -PassThru
$elapsed=[Diagnostics.Stopwatch]::StartNew()
$code=Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /PARENT='+$parent.Id+' /D='+$installed)
if($code -ne 0 -or $elapsed.ElapsedMilliseconds -lt 1200){throw "Automatic handoff did not wait for parent and install: $code"}
if((Get-FileHash -LiteralPath $settings).Hash -ne $settingsHash){throw 'Automatic update changed saved settings'}
foreach($file in $manifest.files){if((Get-FileHash -LiteralPath (Join-Path $installed $file.path)).Hash.ToLowerInvariant() -ne $file.sha256){throw "Automatic update hash mismatch: $($file.path)"}}
# Portable-copy migration uses full setup, never automatic mode or an assumed
# target. Import only into a new installation and keep existing choices intact.
$importSource=Join-Path $testRoot 'portable choices.ini'
$importTarget=Join-Path $testRoot 'new installation'
$importText="[Launcher]`r`nMode=4`r`nPreset=fsr4-4k-quality`r`nFrameGeneration=2`r`nSimulatorHex=0044003a005c0058002d0050006c0061006e0065002000310032`r`n[Updates]`r`nChannel=experimental`r`nAutomaticChecks=0`r`n[FeatureVideo]`r`nAutomatic=0`r`nMuted=1`r`n[Window]`r`nClientWidth=1100`r`nClientHeight=850`r`n"
[IO.File]::WriteAllText($importSource,$importText,[Text.Encoding]::Unicode)
$importHash=(Get-FileHash -LiteralPath $importSource).Hash
$code=Run-Setup ('/S /TEST=1 /IMPORTSETTINGS="'+$importSource+'" /D='+$importTarget)
if($code -ne 0 -or (Get-FileHash -LiteralPath (Join-Path $importTarget 'settings.ini')).Hash -ne $importHash){throw "New installation did not import exact saved choices: $code"}
[IO.File]::WriteAllText($importSource,($importText -replace 'Mode=4','Mode=3'),[Text.Encoding]::Unicode)
$sourceHash=(Get-FileHash -LiteralPath $importSource).Hash
$code=Run-Setup ('/S /TEST=1 /IMPORTSETTINGS="'+$importSource+'" /D='+$importTarget)
if($code -ne 0 -or (Get-FileHash -LiteralPath (Join-Path $importTarget 'settings.ini')).Hash -ne $importHash){throw 'Setup replaced an existing installation settings file during import'}
if((Get-FileHash -LiteralPath $importSource).Hash -ne $sourceHash){throw 'Setup changed the portable source choices'}
foreach($badKind in @('missing','unrelated','oversized')){
 $badSource=Join-Path $testRoot ($badKind+'.ini')
 if($badKind -eq 'unrelated'){[IO.File]::WriteAllText($badSource,'Unrelated file')}
 if($badKind -eq 'oversized'){[IO.File]::WriteAllBytes($badSource,[byte[]]::new(1048577))}
 $badTarget=Join-Path $testRoot ('rejected-import-'+$badKind)
 $code=Run-Setup ('/S /TEST=1 /IMPORTSETTINGS="'+$badSource+'" /D='+$badTarget)
 if($code -ne 8 -or (Test-Path -LiteralPath $badTarget)){throw "Invalid import was not rejected before installation: $badKind / $code"}
}
$code=Run-Setup ('/S /TEST=1 /IMPORTSETTINGS="'+$importSource+'" /D='+$unrelated)
if($code -ne 4 -or @(Get-ChildItem -LiteralPath $unrelated).Count -ne 1){throw 'Choices import bypassed unrelated-folder protection'}
$importUninstaller=Join-Path $testRoot 'import-uninstall.exe'
Copy-Item -LiteralPath (Join-Path $importTarget 'Uninstall.exe') -Destination $importUninstaller
$importUninstall=Start-Process -FilePath $importUninstaller -ArgumentList ('/S _?='+$importTarget) -WindowStyle Hidden -PassThru -Wait
if($importUninstall.ExitCode -ne 0 -or (Get-FileHash -LiteralPath $importSource).Hash -ne $sourceHash){throw 'Imported installation uninstall changed source choices'}
$uninstallCopy = Join-Path $testRoot 'test-uninstall.exe'
Copy-Item -LiteralPath (Join-Path $installed 'Uninstall.exe') -Destination $uninstallCopy
$process = Start-Process -FilePath $uninstallCopy -ArgumentList ('/S _?='+$installed) -WindowStyle Hidden -PassThru -Wait
if($process.ExitCode -ne 0){throw "Uninstall failed: $($process.ExitCode)"}
foreach($file in $manifest.files){if(Test-Path -LiteralPath (Join-Path $installed $file.path)){throw "Installed payload remains: $($file.path)"}}
foreach($file in (@('logs\keep.txt','plugin-backups\keep.txt','user-file.txt')+$nrUserFiles)){if(-not(Test-Path -LiteralPath (Join-Path $installed $file))){throw "User file deleted: $file"}}
$remaining = @(Get-ChildItem -LiteralPath $installed -File -Recurse | ForEach-Object {[IO.Path]::GetRelativePath($installed,$_.FullName)} | Sort-Object)
if(@(Compare-Object ((@('logs\keep.txt','plugin-backups\keep.txt','user-file.txt')+$nrUserFiles) | Sort-Object) $remaining).Count){throw ('Unexpected files after uninstall: '+($remaining -join ', '))}
if($cameraBefore -and (Get-FileHash -LiteralPath $camera).Hash -ne $cameraBefore){throw 'Simulator camera plugin changed'}
if($prefsBefore -and (Get-FileHash -LiteralPath $prefs).Hash -ne $prefsBefore){throw 'Simulator preferences changed'}
$result = [ordered]@{status='PASS';testedAt=(Get-Date -Format o);archiveSha256=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant();archiveBytes=(Get-Item -LiteralPath $archive).Length;payloadHashesVerified=$manifest.files.Count;rarIntegrity='PASS';roundTripExtraction='PASS';invalidSimulatorRejected='PASS';unrelatedFolderRejected='PASS';silentInstall='PASS';recognizedUpdate='PASS';savedChoicesPreservedOnUpdate='PASS';userFilesPreservedOnUninstall='PASS';simulatorCameraUnchanged='PASS';simulatorPreferencesUnchanged='PASS';testMode='Per-user registry and shortcuts skipped; all test files confined to workspace';testFolder=$testRoot}
$result.setupSha256=(Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant()
$result.simulatorFixture='Workspace-only inert files; no live simulator installation used'
$result.setupBytes=(Get-Item -LiteralPath $setup).Length
$result.automaticUpdate='PASS'
$result.automaticUpdateSettingsPreserved='PASS'
$result.parentWait='PASS'
$result.runningLauncherRejected='PASS'
$result.lockedPayloadRejected='PASS'
$result.unownedAutomaticUpdateRejected='PASS'
$result.portableChoicesImported='PASS'
$result.existingSettingsNotOverwrittenByImport='PASS'
$result.invalidImportRejectedBeforeInstall='PASS'
$result.portableSourceChoicesUnchanged='PASS'
if($nrPackage){$result.nrModelsExcluded='PASS';$result.nrModelsAndCachePreserved='PASS';$result.lockedNRBackendRejected='PASS'}
if($featureVideo){
  $result.featureVideoVersion=$featureVideo.version
  $result.featureVideoSha256=$featureVideo.sha256
  $result.featureVideoBytes=$featureVideo.bytes
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot $ResultName) -Encoding utf8
$result | ConvertTo-Json
