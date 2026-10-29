param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$')][string]$Version,
    [Parameter(Mandatory)][string]$Commit,
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$InstallerTestResult,
    [Parameter(Mandatory)][string]$Instructions,
    [string]$SetupInstaller = '',
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
    $setupName = "XPlane-FSR-Ghosting-Fix-$Version-Setup.exe"
    Copy-Item -LiteralPath $SetupInstaller -Destination (Join-Path $output $setupName)
    [IO.File]::AppendAllText((Join-Path $output 'SHA256SUMS.txt'), "$setupHash  $setupName`n", $utf8)
}
$notes = @"
## FSR Ghosting Fix for X-Plane $Version

Experimental Windows Vulkan community preview. Original project code is MIT
licensed; packaged third-party runtimes retain their separate notices.

### Changes

$changes

### Installation

Download the installer RAR, extract it, and run XPlane-FSR24-Setup.exe. Close
X-Plane and the previous launcher first. For a recognized update, choose the
existing app folder; saved launcher choices are preserved. Read the attached
INSTALLATION-AND-USE.txt before starting the simulator.

The standalone Setup.exe asset is also used by the in-app updater. Existing
users install 0.5.4 once; future releases can be checked and installed from
Updates. No archive extractor is needed for that flow.

### Validation and limits

The attached archive passed integrity, extraction, payload hashes,
install/update/uninstall and saved-choice preservation checks. Source commit:
$sourceCommit. A passing fixture does not establish live-flight visual quality.
Cockpit-screen restoration still requires frame generation Off and currently
supports only the default Cirrus SR22. SDK runtimes
are included under their binary redistribution terms; simulator assets and user
data are excluded. Keep all supplied license notices with the installation.

Installer SHA-256: $hash
"@
[IO.File]::WriteAllText((Join-Path $output 'RELEASE-NOTES.md'), $notes, $utf8)
Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length
