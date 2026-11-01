# Publishing application updates

Application updates are also published at
[GitHub Releases](https://github.com/cjboy31/xplane12-fsr-framegen/releases).
Source changes alone do not deliver the Windows installer to users.

Versions 0.6.2 through 0.7.3 shipped feature videos. That workflow and its
source files were removed in v2 (they remain in git history and in those
published releases). Do not move an older tag or replace its assets.

Starting with 0.7.4, the owner retired startup videos. Ship the in-sim changelog
and CHANGELOG.txt instead. No new feature video or video approval is required.
The desktop executable is a plugin updater; image controls remain in X-Plane.

1. Update version metadata and the changelog. Finish relevant source/graphics
   checks and inspect the Windows UI when it changes.
2. Build a complete installer RAR using an explicit file allow-list. Preserve
   component licenses and include plain installation/use instructions. Exclude
   simulator/aircraft assets, reference-mod code, user settings and test data.
3. Verify archive integrity and hashes, installation, recognized updates,
   saved-choice preservation and uninstall behavior in an isolated workspace.
4. Prepare release assets with `Prepare-Release.ps1`. Supply the archive, passing
   installer-test JSON and exact built source commit. It verifies the tested
   archive hash and writes only selected public metadata; it does not upload
   logs or the test result's local folder paths.
5. On GitHub, create a release/tag such as `v0.5.1-alpha` targeting the exact
   built source commit. Mark experimental builds as a **pre-release**. Do not
   present a synthetic GPU result as a verified live-flight improvement.
6. Upload the RAR, `SHA256SUMS.txt` and `INSTALLATION-AND-USE.txt`. Use the generated
   `RELEASE-NOTES.md` as the release body. Keep prior versioned releases.
7. Publish and verify the public page, source tag, asset names/sizes and server
   SHA-256 digest when available. Give the user the release URL.

Starting with 0.5.4, also pass `-SetupInstaller` to Prepare-Release and upload
`XPlane-FSR3-4-FrameGen-<version>-Setup.exe`. Starting with 0.7.8, do not generate
or upload the Ghosting-Fix-named compatibility copy. Older published assets stay
available; users of the 0.5.4 updater can install the new setup manually. The setup must be the exact setup inside
the tested RAR, and automatic-update mode must pass. The updater requires this
exact filename, a v-prefixed semantic tag, uploaded state, HTTPS download path
and GitHub's SHA-256 digest. Keep this asset available: RAR-only releases cannot
be installed by the updater. No separate update server or scheduler is needed.
Update `currentVersion` in src/update_feed.hpp along with the app resource/UI
version. Release preparation rejects a mismatch to prevent repeated self-updates.

Example after a separately tested installer has been built:

```powershell
./Prepare-Release.ps1 -Version '0.5.1-alpha' -Commit '<built-source-sha>' `
  -Archive 'C:\Builds\XPlane-FSR24-Friendly-Presets-Installer.rar' `
  -InstallerTestResult 'C:\Builds\installer-results.json' `
  -Instructions 'C:\Builds\INSTALLATION-AND-USE.txt'
```

This prepares ignored `dist/<version>/` assets. It does not create credentials,
change repository permissions or bypass authentication. Use a connected GitHub
release/upload API when available; otherwise use the normal release editor.


For 0.6.4 and later, release channels are independent of GitHub's prerelease
badge. `alpha`, `beta`, `rc` and final versions belong to Normal;
`experimental.N` belongs to Experimental. Both alpha and experimental packages
must be marked prerelease. Build Normal without flags; build Experimental with
`-FrameGenerationDevelopment -NeuralRenderingDevelopment`. Starting with 0.7.1,
both channels include NR, FG and synchronized compatibility options; build flags
choose the version/channel rather than gating those features. Run Build-NR.ps1
for each staged payload (or copy its verified allow-listed files and manifest),
and pass `-NeuralRenderingDevelopment` to both installer builds. Keep their staged
binaries, version resources, notices, video manifests and installer test evidence
separate. Never upload a development binary under a Normal tag.

After both installer checks pass, run `installer/Test-ReleaseChannels.ps1` with
the exact two setup files. Verify Normal -> Experimental -> Normal transitions,
settings preservation and the correct NR/FG payload in both channels. Older
Normal packages below 0.7.1 still omit FG; do not apply that rule to newer builds.
The selector must offer the published Experimental installer even if its version
is older; same-channel automatic checks must never downgrade or cross channels.
Do not remove old tagged releases. Keep the independent native-motion experiment
separate from the FG development package.

For updater changes, run `Test-UpdateUI.ps1`, `Test-UpdateFlow.ps1` and
`Test-Updater.ps1` with the Windows compiler. The flow checks drive real dialog
commands and background jobs with injected network/process boundaries; they must
cover both installation-marker states, both channel directions, delayed/stale
checks, download cancellation/retry, integrity rejection and installer arguments.
Installer fixtures additionally verify new-install choice imports, preservation
of existing settings and rejection of missing/invalid/oversized import files.
Verify the actual open dropdown and the enabled installation action in the app.

## Release and Beta channels (0.7.8 onward)

The owner's current policy replaces Normal/Experimental with exactly two choices:
Release and Beta. Main contains the delivered NR/FG/compatibility work. Release
includes all current features; Beta previews future changes. Do not withhold an
already merged feature from Release. Preserve image choices and local models.
The updater reads old `normal`/`experimental` preferences as Release/Beta and
writes only `release`/`beta`. Historical releases and tags remain available.

Use numeric tags such as `v0.7.8` for Release and `v0.7.8-beta.windows.1` for the
Windows Beta. Mark Beta as prerelease, Release as a normal release. The Windows
namespace prevents old updaters that treated ordinary beta.N as Normal from
automatically entering Beta. Test both the current parser and the old parser
against the actual public feed. Legacy experimental.N tags remain readable by
the new Beta channel. New Beta tags do not need to be understood by old builds;
those users install the current Release once, then choose Beta if desired.

Build Release without channel flags and Beta with `-Beta` (the old
`-FrameGenerationDevelopment` alias remains for build-script compatibility).
Keep using the reviewed NR component and packaging opt-in in both payloads.
Run both installer suites and `Test-ReleaseChannels.ps1 -ReleaseSetup ...
-BetaSetup ...`. Publish only the RAR, single branded Setup.exe, instructions
and checksums. This policy supersedes the historical Normal/Experimental
instructions above. Linux/macOS platform status must remain explicit.

## Discord community updates

After the GitHub release and asset verification in step 7, announce the published
version in the community's [#updates channel](https://discord.com/channels/1565487749832777830/1565816821523681290).
The owner has authorized routine release announcements there. Check for an
existing post with the same release URL before sending. Include the version,
Normal/Experimental channel, a concise feature summary, any changed setup or
hardware/model requirements, important limitations and the official GitHub
release link. Keep the tone friendly and avoid unsupported quality/FPS claims.
Verify the visible message and save its permalink. New releases use the in-sim
changelog instead of feature-video approval. If access is unavailable, prepare the text and report the pending
announcement rather than claiming success.

## Release Candidates and Betas (v2 onward)

v2 renames the two channels and tightens what goes in each. Saved updater keys
stay `release` / `beta`, so nobody's choice changes.

| Channel | What it may contain | Git branch | Tag examples | GitHub release |
| --- | --- | --- | --- | --- |
| **Release Candidates** | Only features that passed the verification checklist below | `main` | `v2.0.0-rc.1`, then `v2.0.0` | Normal release |
| **Betas** | Features still being developed, tested or verified | `beta` | `v2.0.0-beta.windows.1` | Pre-release |

Promotion from Betas to Release Candidates requires, for each feature:

1. CI green (source policy, Windows CPU/native-UI suite, portable pacing tests).
2. Relevant GPU fixtures pass on real hardware (state GPU + driver).
3. A live X-Plane flight test: same scene with the feature Off and On, logs
   attached, no device loss, no regression in the session log.
4. Installer install / update / uninstall and channel switching re-tested with
   saved choices preserved.
5. Changelog wording describes only what was verified.

Anything not meeting all five stays in Betas. Discord announcements use the
same two names; post Release Candidates and Betas in their own threads or with
the channel name first so users can tell them apart.
