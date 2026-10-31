param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$')][string]$Version,
    [Parameter(Mandatory)][string]$Commit,
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$InstallerTestResult,
    [Parameter(Mandatory)][string]$Instructions,
    [string]$SetupInstaller = '',
    [string]$FeatureVideo = '',
    [string]$Git = 'git'
)
$ErrorActionPreference = 'Stop'
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
if ([IO.Path]::GetExtension($archivePath) -ne '.rar') { throw 'Expected the tested installer RAR' }
$result = Get-Content -LiteralPath $InstallerTestResult -Raw | ConvertFrom-Json
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($result.status -ne 'PASS' -or $result.archiveSha256 -ne $hash -or
    $result.archiveBytes -ne (Get-Item -LiteralPath $archivePath).Length) {
    throw 'Archive does not match the passing installer test result'
}
foreach ($check in @('rarIntegrity','roundTripExtraction','silentInstall','recognizedUpdate',
                     'savedChoicesPreservedOnUpdate','userFilesPreservedOnUninstall')) {
    if ($result.$check -ne 'PASS') { throw "Required installer check did not pass: $check" }
}
$sourceCommit = & $Git -C $PSScriptRoot rev-parse --verify ($Commit+'^{commit}')
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[0-9a-f]{40}$') { throw 'Invalid built source commit' }
$changelog = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CHANGELOG.md') -Raw
$entry = [regex]::Match($changelog, ('(?ms)^##\s+'+[regex]::Escape($Version)+'\r?\n(.*?)(?=^##\s|\z)'))
if (-not $entry.Success) { throw 'Add this version to CHANGELOG.md before preparing a release' }
$changes = $entry.Groups[1].Value.Trim()
if ([version](($Version -split '-')[0]) -ge [version]'0.6.2') {
    if (-not $FeatureVideo) { throw 'This release requires the exact installer-tested feature video' }
    if ($result.featureVideoSha256 -ne (Get-FileHash -LiteralPath $FeatureVideo).Hash.ToLowerInvariant() -or
        $result.featureVideoBytes -ne (Get-Item -LiteralPath $FeatureVideo).Length -or
        $result.featureVideoVersion -ne $Version) {
        throw 'Feature video must match the installed payload verified by Test-Installer'
    }
}
$output = Join-Path (Join-Path $PSScriptRoot 'dist') $Version
if (Test-Path -LiteralPath $output) { throw 'Release staging already exists; keep versioned artifacts intact' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$archiveName = [IO.Path]::GetFileName($archivePath)
Copy-Item -LiteralPath $archivePath -Destination (Join-Path $output $archiveName)
Copy-Item -LiteralPath $Instructions -Destination (Join-Path $output 'INSTALLATION-AND-USE.txt')
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), "$hash  $archiveName`n", $utf8)
if ($SetupInstaller) {
    $updateHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'src\update_feed.hpp') -Raw
    if ($updateHeader -notmatch ('currentVersion\[\]\s*=\s*"'+[regex]::Escape($Version)+'"')) {
        throw 'Updater currentVersion must match the release version'
    }
    $setupHash = (Get-FileHash -LiteralPath $SetupInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($result.setupSha256 -ne $setupHash -or $result.setupBytes -ne (Get-Item -LiteralPath $SetupInstaller).Length -or $result.automaticUpdate -ne 'PASS') {
        throw 'Standalone setup must match the installer tested through automatic-update mode'
    }
    foreach ($setupName in @("XPlane-FSR3-4-FrameGen-$Version-Setup.exe", "XPlane-FSR-Ghosting-Fix-$Version-Setup.exe")) {
        Copy-Item -LiteralPath $SetupInstaller -Destination (Join-Path $output $setupName)
        [IO.File]::AppendAllText((Join-Path $output 'SHA256SUMS.txt'), "$setupHash  $setupName`n", $utf8)
    }
}
if ($FeatureVideo) {
    $videoName = "XPlane-FSR3-4-$Version-Feature-Guide.mp4"
    Copy-Item -LiteralPath $FeatureVideo -Destination (Join-Path $output $videoName)
    $videoHash = (Get-FileHash -LiteralPath $FeatureVideo).Hash.ToLowerInvariant()
    [IO.File]::AppendAllText((Join-Path $output 'SHA256SUMS.txt'), "$videoHash  $videoName`n", $utf8)
}
$channelSummary = if([version](($Version -split '-')[0]) -ge [version]'0.7.1' -and $result.nrModelsExcluded -eq 'PASS'){
    $channel=if($Version -match '-experimental\.'){'Experimental: future development track.'}else{'Normal: recommended current feature release.'}
    "$channel Both 0.7.1 channels include FSR AA/upscaling, independent NR, frame generation and synchronized compatibility options. Update to Normal for all current features. NR and FG start Off; NR needs compatible Windows RDNA4 FP8 hardware and your own supported local model. NR + FG requires explicit opt-in and retains visual/pacing limits. No NVIDIA DLL or model weights are included."
}elseif($result.nrModelsExcluded -eq 'PASS'){
    'Experimental NR channel: optional independent neural rendering for compatible Windows RDNA4 FP8 hardware. NR starts Off; import your own supported model locally. This is not official NVIDIA DLSS 5 support. NR + FG requires explicit experimental opt-in and remains subject to visual/pacing limitations.'
}elseif($Version -match '-experimental\.'){
    'Experimental channel: opt-in frame-generation development. FG artifacts/flicker remain possible; this is not a new interpolation or pacing fix. Use Updates / Channel to return to Normal.'
}else{
    'Normal channel: recommended project configuration, still an alpha preview. Built-in frame generation stays disabled. Use Updates / Channel to find the separate Experimental installer.'
}
$guideSummary = "Read the included transcript and media provenance for this guide's content and capture limits."
if($FeatureVideo){
    $provenancePath=Join-Path (Split-Path $FeatureVideo -Parent) 'PROVENANCE.json'
    if(Test-Path -LiteralPath $provenancePath){
        $provenance=Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
        if($provenance.project.version -ne $Version -or $provenance.movie_sha256 -ne $videoHash){throw 'Guide provenance does not match this version and exact video'}
        $seconds=[int][Math]::Round([double]$provenance.seconds)
        if($provenance.approved_guide_reuse){
            $guideSummary='This maintenance package reuses a previously approved guide unchanged, including its original on-screen version. Current fixes and setup behavior are described above and in the instructions. No revised film or new rendering-quality comparison is claimed.'
        }elseif($provenance.project.capture.text_only){
            $guideSummary="This $seconds-second guide uses original text and aviation graphics to explain renderer selection, saved choices and setup. It contains no simulator comparison or performance demonstration."
        }elseif($provenance.project.capture.background_only){
            $guideSummary="This $seconds-second guide explains this version's features and setup. A previously reviewed simulator still is used only as background; it is not an NR/FG before-after comparison or a performance demonstration."
        }else{
            $guideSummary="This $seconds-second guide includes the capture examples described in its provenance. They are individual detail examples, not benchmarks or general ghosting-fix guarantees."
        }
    }
}
$notes = @"
## FSR 3/4 + Frame Gen for X-Plane $Version

Experimental Windows Vulkan community preview. Original project code is MIT
licensed; packaged third-party runtimes retain their separate notices.

$channelSummary

### Changes

$changes

### Installation

Download the installer RAR, extract it, and run XPlane-FSR24-Setup.exe. Close
X-Plane and the previous launcher first. For a recognized update, choose the
existing app folder; saved launcher choices are preserved. Read the attached
INSTALLATION-AND-USE.txt before starting the simulator.

The standalone Setup.exe asset is also used by the in-app updater. Users with
built-in Updates can select Check now. Users on older versions install this
version once. No archive extractor is needed for in-app installation.

Choose XPlane-FSR3-4-FrameGen-$Version-Setup.exe for a direct download. The
Ghosting-Fix-named setup is an identical compatibility copy for the 0.5.4 updater;
it installs the newly named app. Ghosting Fix remains the cockpit feature's name.

### Validation and limits

The attached archive passed integrity, extraction, payload hashes,
install/update/uninstall and saved-choice preservation checks. Source commit:
$sourceCommit. A passing fixture does not establish live-flight visual quality.
Cockpit-screen restoration still requires frame generation Off and currently
supports only the default Cirrus SR22. SDK runtimes
are included under their binary redistribution terms; simulator source assets
and user data are excluded. Any simulator visuals retain their owners' rights.
Keep all supplied license notices.

### Feature guide

The captioned, narrated MP4 plays offline once per installed version. Skip it
immediately, disable future videos, or replay from What's new. $guideSummary
The version-specific changes and validation scope are listed above.
The separate Linux/Mac preview packages are unchanged.

Installer SHA-256: $hash
"@
[IO.File]::WriteAllText((Join-Path $output 'RELEASE-NOTES.md'), $notes, $utf8)
Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length
