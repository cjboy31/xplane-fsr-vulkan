param(
  [string]$WorkspaceRoot = (Split-Path $PSScriptRoot -Parent),
  [string]$Nsis = (Join-Path $PSScriptRoot '..\work\toolchain\nsis-portable\nsis-3.13\makensis.exe'),
  [string]$Rar = 'C:\Program Files\WinRAR\Rar.exe',
  [string]$ReleaseFolder = 'xplane-fsr24-fg-alpha',
  [string]$PackageFolder = 'XPlane-FSR24-Installation',
  [string]$ArchiveName = 'XPlane-FSR24-Installer.rar',
  [string]$DocumentationFolder = $PSScriptRoot,
  [string]$PackageVersion = '0.4.0-alpha',
  [string]$WindowsVersion = '0.4.0.0',
  [string[]]$AdditionalOriginalFiles = @(),
  [string]$IconPath = '',
  [string]$SetupScript = (Join-Path $PSScriptRoot 'Setup.nsi'),
  [string]$MingwNotices = '',
  [string]$FeatureVideoDirectory = '',
  [switch]$NeuralRenderingDevelopment
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$release = Join-Path (Join-Path $taskRoot 'outputs') $ReleaseFolder
$app = Join-Path $release 'app'
$generated = Join-Path $PSScriptRoot ('build-'+$PackageVersion)
$payload = Join-Path $generated 'payload'
$package = Join-Path (Join-Path $taskRoot 'outputs') $PackageFolder
$mingw = Join-Path $taskRoot 'work\toolchain\llvm-mingw-20261020-ucrt-x86_64\x86_64-w64-mingw32\share\mingw32'
if($MingwNotices){$mingw=$MingwNotices}
New-Item -ItemType Directory -Force -Path $generated,$payload,$package,(Join-Path $payload 'licenses'),(Join-Path $package 'licenses') | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Text([string]$Path,[string]$Content) { [IO.File]::WriteAllText($Path,($Content -replace "`r?`n","`r`n"),$utf8) }
$entries = [Collections.Generic.List[object]]::new()
function Add-Payload([string]$Source,[string]$Relative,[string]$License) {
  $dest = Join-Path $payload $Relative
  New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $dest -Force
  $entries.Add([pscustomobject]@{path=$Relative.Replace('\','/');sha256=(Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $dest).Length;licenseGroup=$License})
}
$originalFiles = @('XPlaneFSR24.exe','NativeAACheck.exe','CockpitRestoreCheck.exe','layer\XPFSR24Layer.dll','layer\XPFSR24Layer.json','plugin\64\win.xpl') + $AdditionalOriginalFiles
if([version](($PackageVersion -split '-')[0]) -ge [version]'0.7.2'){$originalFiles += @('IntegrationSetup.exe','auto\XPFSR24Implicit.json')}
if([version](($PackageVersion -split '-')[0]) -ge [version]'0.7.4'){$originalFiles += @('CHANGELOG.txt','NativeAAUpdater.exe','extras/shadows/make XP shadows great again - enhanced edition.lua','extras/shadows/README.txt')}
$fgDevelopment = $PackageVersion -match '^\d+\.\d+\.\d+-(?:experimental|beta)(?:\.windows)?\.\d+$'
$fgAvailable = $fgDevelopment -or [version](($PackageVersion -split '-')[0]) -ge [version]'0.7.1'
if($fgAvailable){$originalFiles += 'FrameGenCheck.exe'}
$builtVersion = (Get-Item -LiteralPath (Join-Path $app 'XPlaneFSR24.exe')).VersionInfo.FileVersion
if($builtVersion -ne $PackageVersion){throw "App resource version $builtVersion does not match package $PackageVersion"}
foreach($relative in $originalFiles){ Add-Payload (Join-Path $app $relative) $relative 'Original MIT; API/compiler-runtime notices in licenses' }
if($NeuralRenderingDevelopment){
 if(-not $fgDevelopment -and [version](($PackageVersion -split '-')[0]) -lt [version]'0.7.1'){throw 'Normal NR packages require version 0.7.1 or later'}
 $nrManifest=Join-Path $app 'nr\NR-BUILD.json'
 $nr=Get-Content -LiteralPath $nrManifest -Raw | ConvertFrom-Json
 if($nr.sourceCommit -ne '960038fd1cb892742548f408355e0f3b2fd30d6f' -or $nr.modelsIncluded -ne $false -or $nr.acoIncluded -ne $false -or $nr.files.Count -lt 70){throw 'Unreviewed NR component manifest'}
 $seen=@{}
 foreach($entry in $nr.files){
  if($entry.path -notmatch '^(XPFSRNeuralRendering\.dll|ImportNRModel\.exe|NRCheck\.exe|dlssnr-amd/shaders/(?:[a-z0-9_-]+/)?[a-z0-9_-]+\.(spv|txt))$' -or $seen.ContainsKey($entry.path)){throw 'NR payload is outside the explicit code/shader allow-list'}
  $seen[$entry.path]=$true
  $source=Join-Path $app ('nr/'+$entry.path)
  if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $entry.sha256 -or (Get-Item -LiteralPath $source).Length -ne $entry.bytes){throw 'NR payload integrity mismatch'}
  Add-Payload $source ('nr/'+$entry.path) 'Original adapter + pinned DLSSNR-AMD MIT; FSR1 MIT shader notices'
 }
 foreach($required in @('XPFSRNeuralRendering.dll','ImportNRModel.exe','NRCheck.exe')){if(-not $seen.ContainsKey($required)){throw 'NR component is incomplete'}}
 Add-Payload $nrManifest 'nr\NR-BUILD.json' 'Original MIT build provenance; no models'
 foreach($name in @('DLSSNR-AMD-MIT.txt','DLSSNR-AMD-UPSTREAM-NOTICES.md','FidelityFX-FSR1-MIT.txt')){Add-Payload (Join-Path $app ('licenses/'+$name)) ('licenses/'+$name) 'Upstream MIT and component notices, preserved intact'}
}
if([version](($PackageVersion -split '-')[0]) -ge [version]'0.6.2' -and [version](($PackageVersion -split '-')[0]) -lt [version]'0.7.4') {
  if(-not $FeatureVideoDirectory){throw 'A matching feature video is required for every Windows release starting with 0.6.2'}
  $videoFile = Join-Path $FeatureVideoDirectory ($PackageVersion+'.mp4')
  $videoManifest = Join-Path $FeatureVideoDirectory 'feature-video.ini'
  $videoText = [IO.File]::ReadAllText($videoManifest)
  $videoDigest = (Get-FileHash -LiteralPath $videoFile -Algorithm SHA256).Hash.ToLowerInvariant()
  $videoBytes = (Get-Item -LiteralPath $videoFile).Length
  if($videoText -notmatch ('(?m)^Version='+[regex]::Escape($PackageVersion)+'\r?$') -or
     $videoText -notmatch ('(?m)^SHA256='+$videoDigest+'\r?$') -or
     $videoText -notmatch ('(?m)^Bytes='+$videoBytes+'\r?$') -or $videoBytes -lt 1024 -or $videoBytes -gt 100663296) {
    throw 'Feature video manifest must match this version and exact MP4 bytes/hash'
  }
  foreach($name in @(($PackageVersion+'.mp4'),'feature-video.ini','TRANSCRIPT.txt','CAPTIONS.srt','PROVENANCE.json','CREDITS-AND-LICENSES.txt')) {
    Add-Payload (Join-Path $FeatureVideoDirectory $name) ('media\'+$name) 'Original edit/narration/artwork; captured simulator visuals retain their owners; see media/CREDITS-AND-LICENSES.txt'
  }
}
$expectedRuntime = @{
 'amd_fidelityfx_loader_vk.dll'='38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9'
 'amd_fidelityfx_upscaler_vk.dll'='4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7'
}
if($fgAvailable){
 $expectedRuntime['amd_fidelityfx_framegeneration_vk.dll']='d002dabff8c5e71eaa715fc99438cd4dc81230262a7897b16bc402efec3498b4'
}
foreach($name in ($expectedRuntime.Keys | Sort-Object)){
  $source = Join-Path $app ('runtime\'+$name)
  if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $expectedRuntime[$name]){throw "Unexpected AMD runtime: $name"}
  $signature = Get-AuthenticodeSignature -LiteralPath $source
  if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices'){throw "AMD signature check failed: $name"}
  Add-Payload $source ('runtime\'+$name) 'AMD SDK binary redistribution terms; AMD-SDK-license.md and third-party notices'
}
$licenseFiles = @('AMD-API-Headers-MIT.txt','AMD-SDK-license.md','AMD-SDK-third-party-notices.md','LLVM-MinGW-LICENSE.TXT','Vulkan-Headers-Apache-2.0.txt','Vulkan-Headers-LICENSE.md','Vulkan-Headers-MIT.txt','XPLM-SDK-license.txt')
foreach($name in $licenseFiles){ Add-Payload (Join-Path $app ('licenses\'+$name)) ('licenses\'+$name) 'Upstream license/notice text, preserved intact' }
foreach($name in @('COPYING','COPYING.MinGW-w64.txt','COPYING.MinGW-w64-runtime.txt','COPYING.winpthreads.txt','COPYING.winstorecompat.txt')){
  $outputName = if($name -eq 'COPYING'){'MinGW-COPYING.txt'}else{'MinGW-'+$name}
  Add-Payload (Join-Path $mingw $name) ('licenses\'+$outputName) 'Upstream MinGW runtime license/notice text, preserved intact'
}
Add-Payload (Join-Path (Split-Path $Nsis -Parent) 'COPYING') 'licenses\NSIS-COPYING.txt' 'Upstream NSIS license/notice text, preserved intact'
Add-Payload (Join-Path $release 'LICENSE') 'LICENSE.txt' 'Original MIT notice'
Add-Payload (Join-Path $DocumentationFolder 'INSTALLATION-AND-USE.txt') 'INSTALLATION-AND-USE.txt' 'Original MIT documentation'
Add-Payload (Join-Path $DocumentationFolder 'REDISTRIBUTION.txt') 'REDISTRIBUTION.txt' 'Original MIT documentation'
$notice = @'
THIRD-PARTY NOTICES

See REDISTRIBUTION.txt for the file-by-file component review, upstream license
locations and exclusions. The original mod is MIT licensed in LICENSE.txt.
AMD runtime binaries have separate binary redistribution terms in
licenses/AMD-SDK-license.md and licenses/AMD-SDK-third-party-notices.md.
All upstream notices in this folder are included intact. Vulkan, XPLM,
LLVM-MinGW and NSIS retain their respective licenses. No endorsement is claimed.
'@
Write-Text (Join-Path $generated 'THIRD-PARTY-NOTICES.txt') $notice
Add-Payload (Join-Path $generated 'THIRD-PARTY-NOTICES.txt') 'licenses\THIRD-PARTY-NOTICES.txt' 'Original MIT index of upstream notices'
# Keep this legacy ownership identity stable so previous installers can repair
# or roll back a recognized package. It is not a channel label; use channel.
$manifest = [ordered]@{product='FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)';channel=$(if($fgDevelopment){'Beta'}else{'Release'});packageVersion=$PackageVersion;reviewDate='2026-10-31';hashAlgorithm='SHA-256';scope='Installed payload except this manifest. Its hash is recorded by the outer package manifest. Uninstaller, settings and ownership marker are generated at install time.';files=@($entries | Sort-Object path)}
Write-Text (Join-Path $payload 'PAYLOAD-MANIFEST.json') ($manifest | ConvertTo-Json -Depth 5)
$allFiles = @($entries.path) + @('PAYLOAD-MANIFEST.json')
# Reject unexpected staging files instead of silently including a development file.
$actual = @(Get-ChildItem -LiteralPath $payload -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($payload,$_.FullName).Replace('\','/') } | Sort-Object)
if(@(Compare-Object ($allFiles | ForEach-Object {$_.Replace('\','/')} | Sort-Object) $actual).Count){throw 'Payload staging contains files outside the allow-list'}
$install = [Collections.Generic.List[string]]::new()
$uninstall = [Collections.Generic.List[string]]::new()
foreach($relative in ($allFiles | Sort-Object)){
  $winRelative = $relative.Replace('/','\')
  $parent = Split-Path $winRelative -Parent
  $target = if($parent){'$INSTDIR\'+$parent}else{'$INSTDIR'}
  $install.Add('SetOutPath "'+$target+'"')
  $install.Add('File "'+(Join-Path $payload $winRelative)+'"')
  $uninstall.Add('Delete "$INSTDIR\'+$winRelative+'"')
}
$directories=@{}
foreach($relative in $allFiles){
 $parent=Split-Path $relative.Replace('/','\') -Parent
 while($parent){$directories[$parent]=$true;$parent=Split-Path $parent -Parent}
}
foreach($directory in ($directories.Keys | Sort-Object Length -Descending)){ $uninstall.Add('RMDir "$INSTDIR\'+$directory+'"') }
Write-Text (Join-Path $generated 'InstallFiles.nsh') ($install -join "`n")
Write-Text (Join-Path $generated 'UninstallFiles.nsh') ($uninstall -join "`n")
[string[]]$iconArguments = @()
if($IconPath){$iconArguments += ('/DAPP_ICON='+(Resolve-Path -LiteralPath $IconPath).Path)}
if($fgDevelopment){$iconArguments += @('/DFG_DEVELOPMENT=1','/DCHANNEL_LABEL=Beta')}else{$iconArguments += '/DCHANNEL_LABEL=Release Candidate'}
if($fgAvailable){$iconArguments += '/DFG_AVAILABLE=1'}
if($NeuralRenderingDevelopment){$iconArguments += '/DNR_DEVELOPMENT=1'}
& $Nsis '/V3' ('/DPACKAGE='+$package) ('/DGENERATED='+$generated) ('/DPACKAGE_VERSION='+$PackageVersion) ('/DWINDOWS_VERSION='+$WindowsVersion) @iconArguments $SetupScript
if($LASTEXITCODE -ne 0){throw 'NSIS compilation failed'}
Copy-Item -LiteralPath (Join-Path $DocumentationFolder 'INSTALLATION-AND-USE.txt') -Destination (Join-Path $package 'READ-ME-FIRST.txt') -Force
Copy-Item -LiteralPath (Join-Path $DocumentationFolder 'REDISTRIBUTION.txt') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $payload 'LICENSE.txt') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $payload 'PAYLOAD-MANIFEST.json') -Destination $package -Force
Get-ChildItem -LiteralPath (Join-Path $payload 'licenses') -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $package 'licenses') -Force}
$hashes = @(Get-ChildItem -LiteralPath $package -File -Recurse | Where-Object Name -ne 'PACKAGE-SHA256.txt' | Sort-Object FullName | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetRelativePath($package,$_.FullName).Replace('\','/') })
Write-Text (Join-Path $package 'PACKAGE-SHA256.txt') ($hashes -join "`n")
$archive = Join-Path (Join-Path $taskRoot 'outputs') $ArchiveName
if(Test-Path -LiteralPath $archive){throw 'Archive already exists. Preserve it or explicitly remove the verified old output before rebuilding.'}
Push-Location (Split-Path $package -Parent)
try { & $Rar a '-r' '-ma5' '-m3' '-ep1' $archive $PackageFolder; if($LASTEXITCODE -ne 0){throw 'RAR creation failed'} }
finally { Pop-Location }
& $Rar t '-idq' $archive
if($LASTEXITCODE -ne 0){throw 'RAR integrity test failed'}
Get-Item -LiteralPath $archive | Select-Object FullName,Length
Get-FileHash -LiteralPath $archive
