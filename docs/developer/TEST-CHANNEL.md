# Private Test channel

Owner-only. Test builds are checked here before anything reaches **Betas**, and
nothing reaches **Release Candidates** until it has passed both.

```
test branch ──► draft release test-<version>.<n> ──► live test ──► beta branch / Betas ──► main / Release Candidates
```

## Why it stays private

- Test builds are **draft** GitHub releases. Only accounts with write access to
  the repository can see or download them.
- Tags start with `test-`, never `v`. The in-sim updater skips drafts and any
  tag that is not `v…`, so it cannot offer a test build even if one is
  published by mistake. `src/update_tests.cpp` checks this.
- The `test` branch is not linked from the README.

## Publishing a test build (owner or Claude)

1. Merge the change into `test` and build the Windows package as usual
   (`build.ps1`, then `installer/Build-Installer.ps1 -Beta`).
2. Run
   `./Publish-TestBuild.ps1 -Version 2.0.0 -Files <Setup.exe> -Notes '<what changed and what to test>'`.
   It numbers the build (`test-2.0.0.1`, `.2`, ...), uploads the files with
   `SHA256SUMS.txt` and a note telling the tester what to focus on.

## Testing a build (ChatGPT or another tester)

You need to be signed in to GitHub as the owner (or with a token that has
access to this repository), because drafts are private.

1. Open **Releases**, find the newest `TEST … (private)` draft, download the
   files and check them against `SHA256SUMS.txt`.
2. Close X-Plane. Run the Setup.exe. Let it find X-Plane (or browse to the folder
   that contains `X-Plane.exe`). Keep "Load the mod automatically" ticked.
3. Start X-Plane normally, load the **same saved flight every time** (same
   airport, aircraft, time and weather). Open **Plugins > FSR 3.1 / 4.1 + Frame Gen > Open settings**.
4. Run the checks in the release note first, then this baseline:

| # | Check | Pass when |
| --- | --- | --- |
| 1 | Panel opens; status reads **FSR ACTIVE** | Shown within a few seconds of loading the flight |
| 2 | Point at any **?** | A help card fades in next to it; settings stay on screen; click pins it; Esc closes it |
| 3 | Same view, FSR On vs Off | No crash, no black frames, picture returns when switched back |
| 4 | Frame generation On, each **Frame pacing** choice, restart between | Diagnostics shows generated frames; the session log has a `FG pacing profile:` line saying `applied` |
| 5 | 2-minute pan over a busy airport, then fly away | No device lost / crash; note min/avg FPS and any stutter |
| 6 | Updater: switch Release Candidates ↔ Betas | Saved choice survives a restart; no test build is offered |
| 7 | Uninstall, then reinstall | Saved image settings are kept; X-Plane starts normally without the mod after uninstall |

5. Collect `Output/FSR-Logs` (Diagnostics > Open log folder) and X-Plane's `Log.txt`.

## Report template

```
Build: test-<version>.<n>   Commit: <sha>
System: GPU + driver, CPU, RAM, Windows version, monitor resolution + refresh, VRR on/off
X-Plane: version, aircraft, airport, weather
Results: 1 PASS/FAIL ... 7 PASS/FAIL (one line each, with numbers where asked)
Problems: what you saw, when, and how to repeat it
Logs: attached / link
Verdict: PROMOTE TO BETAS  or  NEEDS FIX
```

A **PROMOTE TO BETAS** verdict moves the change to the `beta` branch. Promotion
from Betas to Release Candidates still needs the checklist in
[RELEASING.md](RELEASING.md#release-candidates-and-betas-v2-onward).
