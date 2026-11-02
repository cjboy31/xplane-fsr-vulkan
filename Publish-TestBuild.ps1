# SPDX-License-Identifier: MIT
# Private Test channel: upload locally built files as a DRAFT GitHub release.
# Drafts are visible only to people with write access to this repository, and
# the in-sim updater ignores drafts and any tag that does not start with "v",
# so a test build can never reach Release Candidate or Beta users.
#
# Example:
#   ./Publish-TestBuild.ps1 -Version 2.0.0 -Files dist\XPlane-FSR3-4-FrameGen-2.0.0-beta.windows.1-Setup.exe `
#       -Notes 'Frame pacing profiles + hover help. Test FG on RX 9070 at 144 Hz.'
# Requires the GitHub CLI (gh) signed in to an account with write access.
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][string[]]$Files,
    [string]$Notes = '',
    [string]$Repository = '',
    [string]$Commit = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'Install the GitHub CLI (gh) and run: gh auth login' }
if (-not $Repository) { $Repository = (gh repo view --json nameWithOwner -q .nameWithOwner) }
if (-not $Commit) { $Commit = (git rev-parse HEAD).Trim() }
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'test') { Write-Warning "Publishing from '$branch'. Test builds normally come from the 'test' branch." }

# Next free test number for this version: test-<version>.<n>
$existing = gh release list -R $Repository --limit 200 --json tagName -q '.[].tagName' | Where-Object { $_ -like "test-$Version.*" }
$numbers = @($existing | ForEach-Object { [int](($_ -split '\.')[-1]) })
$next = if ($numbers.Count) { ($numbers | Measure-Object -Maximum).Maximum + 1 } else { 1 }
$tag = "test-$Version.$next"

$work = Join-Path ([IO.Path]::GetTempPath()) ("test-build-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $sums = foreach ($f in $Files) {
        $item = Get-Item -LiteralPath $f
        '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash.ToLowerInvariant(), $item.Name
    }
    $sumFile = Join-Path $work 'SHA256SUMS.txt'
    Set-Content -LiteralPath $sumFile -Value $sums -Encoding ascii
    # Open tester prompts travel with every draft so the tester finds them in the build.
    $promptsPath = Join-Path $PSScriptRoot 'docs/developer/TESTER-PROMPTS.md'
    $prompts = if (Test-Path -LiteralPath $promptsPath) {
        $text = Get-Content -LiteralPath $promptsPath -Raw
        $open = [regex]::Match($text, '(?ms)^## Open\s*(.*?)(?=^## Done|\z)')
        if ($open.Success) { $open.Groups[1].Value.Trim() } else { '' }
    } else { '' }
    $body = @"
PRIVATE TEST BUILD - not for public use. Draft release; the updater never offers it.

Source commit: $Commit ($branch)
Version under test: $Version (test $next)

What changed:
$Notes

Tester: start with docs/developer/TESTER-PROMPTS.md (open prompts copied below), follow
docs/developer/TEST-CHANNEL.md and reply with the report template.

## Open tester prompts

$prompts
"@
    $notesFile = Join-Path $work 'NOTES.md'
    Set-Content -LiteralPath $notesFile -Value $body -Encoding utf8
    gh release create $tag @Files $sumFile -R $Repository --draft --prerelease --target $Commit --title "TEST $Version.$next (private)" --notes-file $notesFile
    if ($LASTEXITCODE -ne 0) { throw 'gh release create failed' }
    Write-Output "Draft test release created: $tag (visible only to repository collaborators)."
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
