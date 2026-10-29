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
$regular = Join-Path $testRoot 'regular-channel-install'
New-Item -ItemType Directory -Path $regular | Out-Null
[IO.File]::WriteAllText((Join-Path $regular 'XPFSR24-Installation.ini'),"[Installation]`r`nProduct=XPFSR24-community-alpha`r`n")
[IO.File]::WriteAllText((Join-Path $regular 'keep.txt'),'Regular installation must remain available.')
$regularBefore = (Get-FileHash -LiteralPath (Join-Path $regular 'XPFSR24-Installation.ini')).Hash
$code = Run-Setup ('/S /TEST=1 /XPLANE= /D='+$regular)
if($code -ne 4 -or @(Get-ChildItem -LiteralPath $regular).Count -ne 2 -or (Get-FileHash -LiteralPath (Join-Path $regular 'XPFSR24-Installation.ini')).Hash -ne $regularBefore){throw 'Experimental setup did not preserve/refuse the regular installation'}
$code = Run-Setup ('/S /TEST=1 /XPLANE="'+(Join-Path $testRoot 'no-simulator')+'" /D='+$invalid)
if($code -ne 3){throw "Invalid simulator path was not rejected with code 3: $code"}
if(Test-Path -LiteralPath $invalid){throw 'Invalid-path installation created files'}
$simulator = 'D:\X-Plane 12'
$camera = Join-Path $simulator 'Resources\plugins\XPFSR24\64\win.xpl'
$cameraBefore = if(Test-Path -LiteralPath $camera){(Get-FileHash -LiteralPath $camera).Hash}else{''}
$prefs = Join-Path $simulator 'Output\preferences\X-Plane.prf'
$prefsBefore = if(Test-Path -LiteralPath $prefs){(Get-FileHash -LiteralPath $prefs).Hash}else{''}
$code = Run-Setup ('/S /TEST=1 /XPLANE="'+$simulator+'" /D='+$installed)
if($code -ne 0){throw "Installation failed: $code"}
$manifest = Get-Content -LiteralPath (Join-Path $installed 'PAYLOAD-MANIFEST.json') -Raw | ConvertFrom-Json
foreach($required in @('XPlaneFSR24.exe','NativeAACheck.exe','FrameGenCheck.exe','CockpitRestoreCheck.exe','layer/XPFSR24Layer.dll','plugin/64/win.xpl')){
  if($required -notin $manifest.files.path){throw "Required app component absent from installer: $required"}
}
foreach($file in $manifest.files){
  $path = Join-Path $installed $file.path
  if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $file.sha256){throw "Installed hash mismatch: $($file.path)"}
}
if((Get-Content -LiteralPath (Join-Path $installed 'settings.ini') -Raw) -notmatch [regex]::Escape('Simulator='+$simulator)){throw 'Simulator path was not saved'}
# Verify a recognized update keeps local data and still produces identical files.
New-Item -ItemType Directory -Path (Join-Path $installed 'logs'),(Join-Path $installed 'plugin-backups') | Out-Null
Set-Content -LiteralPath (Join-Path $installed 'logs\keep.txt') -Value 'Test user log'
Set-Content -LiteralPath (Join-Path $installed 'plugin-backups\keep.txt') -Value 'Test user backup'
Set-Content -LiteralPath (Join-Path $installed 'user-file.txt') -Value 'Test unrelated user file'
Add-Content -LiteralPath (Join-Path $installed 'settings.ini') -Value "`r`n[Launcher]`r`nPreset=fsr4-4k-quality`r`nMode=4`r`nMotionSource=1`r`nFrameGeneration=2`r`nCockpitRestore=0`r`nEnabled=1`r`nGhostReduction=0`r`nAlignmentTint=0`r`n[Window]`r`nClientWidth=900`r`nClientHeight=740"
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
Add-Content -LiteralPath $settings -Value "`r`n[Updates]`r`nAutomaticChecks=0`r`nPreviewReleases=0"
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
$parent = Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoProfile','-Command','Start-Sleep -Milliseconds 1800') -WindowStyle Hidden -PassThru
$elapsed=[Diagnostics.Stopwatch]::StartNew()
$code=Run-Setup ('/S /TEST=1 /AUTOUPDATE=1 /PARENT='+$parent.Id+' /D='+$installed)
if($code -ne 0 -or $elapsed.ElapsedMilliseconds -lt 1200){throw "Automatic handoff did not wait for parent and install: $code"}
if((Get-FileHash -LiteralPath $settings).Hash -ne $settingsHash){throw 'Automatic update changed saved settings'}
foreach($file in $manifest.files){if((Get-FileHash -LiteralPath (Join-Path $installed $file.path)).Hash.ToLowerInvariant() -ne $file.sha256){throw "Automatic update hash mismatch: $($file.path)"}}
$uninstallCopy = Join-Path $testRoot 'test-uninstall.exe'
Copy-Item -LiteralPath (Join-Path $installed 'Uninstall.exe') -Destination $uninstallCopy
$process = Start-Process -FilePath $uninstallCopy -ArgumentList ('/S _?='+$installed) -WindowStyle Hidden -PassThru -Wait
if($process.ExitCode -ne 0){throw "Uninstall failed: $($process.ExitCode)"}
foreach($file in $manifest.files){if(Test-Path -LiteralPath (Join-Path $installed $file.path)){throw "Installed payload remains: $($file.path)"}}
foreach($file in @('logs\keep.txt','plugin-backups\keep.txt','user-file.txt')){if(-not(Test-Path -LiteralPath (Join-Path $installed $file))){throw "User file deleted: $file"}}
$remaining = @(Get-ChildItem -LiteralPath $installed -File -Recurse | ForEach-Object {[IO.Path]::GetRelativePath($installed,$_.FullName)} | Sort-Object)
if(@(Compare-Object (@('logs\keep.txt','plugin-backups\keep.txt','user-file.txt') | Sort-Object) $remaining).Count){throw ('Unexpected files after uninstall: '+($remaining -join ', '))}
if($cameraBefore -and (Get-FileHash -LiteralPath $camera).Hash -ne $cameraBefore){throw 'Simulator camera plugin changed'}
if($prefsBefore -and (Get-FileHash -LiteralPath $prefs).Hash -ne $prefsBefore){throw 'Simulator preferences changed'}
$result = [ordered]@{status='PASS';testedAt=(Get-Date -Format o);archiveSha256=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant();archiveBytes=(Get-Item -LiteralPath $archive).Length;payloadHashesVerified=$manifest.files.Count;rarIntegrity='PASS';roundTripExtraction='PASS';invalidSimulatorRejected='PASS';unrelatedFolderRejected='PASS';silentInstall='PASS';recognizedUpdate='PASS';savedChoicesPreservedOnUpdate='PASS';userFilesPreservedOnUninstall='PASS';simulatorCameraUnchanged='PASS';simulatorPreferencesUnchanged='PASS';testMode='Per-user registry and shortcuts skipped; all test files confined to workspace';testFolder=$testRoot}
$result.setupSha256=(Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant()
$result.setupBytes=(Get-Item -LiteralPath $setup).Length
$result.regularInstallationPreservedAndRejected='PASS'
$result.automaticUpdate='PASS'
$result.automaticUpdateSettingsPreserved='PASS'
$result.parentWait='PASS'
$result.runningLauncherRejected='PASS'
$result.lockedPayloadRejected='PASS'
$result.unownedAutomaticUpdateRejected='PASS'
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot $ResultName) -Encoding utf8
$result | ConvertTo-Json
