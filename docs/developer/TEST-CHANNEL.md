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

## GPU fixture identity

Run `Test-FrameGeneration.ps1` with the freshly built `-AppDirectory`. It
isolates implicit-layer discovery for its child processes, verifies the loaded
renderer path and SHA-256 inside the presentation fixture, and exercises all
three pacing profiles on both SDK presenters. Pass an older installed renderer
as `-RejectedLayer <DLL>` to prove the guard rejects it without changing any
registration. A deliberately wrong digest must fail too.

The script restores inherited Vulkan/renderer environment variables even after
failure. `DISABLE_XPFSR24=1` cannot isolate the FSR candidate: it disables explicit
candidate processing as well. The fixture clears it while isolating implicit
discovery with `VK_IMPLICIT_LAYER_PATH` and `VK_LOADER_LAYERS_DISABLE=~implicit~`.

Native renderer motion: run `Test-NativeMotion.ps1` (capture and restore with
stand-in shaders), then, after `Test.ps1`, `Test-NativeMotion.ps1 -Integration`.
It uses the same candidate isolation. The `native-missing` case must log the
camera-derived fallback and must never claim native motion. Details are in
[NATIVE-MOTION-EXPERIMENT](NATIVE-MOTION-EXPERIMENT.md).

SDK generated-image readback, layer swapchain lifecycle and live simulator
generation are separate results. An accepted pacing profile or lifecycle pass
does not establish generated frames, scanout quality or stutter-free flight.

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
| 8 | **Motion source: Native (experimental)**, MSAA Off, frame generation Off; restart | Status reads `EXPERIMENTAL native renderer motion (SSR)`. The log has `Native renderer motion captured` and `Motion source now native renderer`. No crash or black frames |
| 9 | Same flight, Camera vs Native: 30 s slow turn with the external view, then the cockpit view while turning | Note which trails less around the airframe and cockpit edges. Note any new smearing of scenery |
| 10 | Native selected, then each of: frame generation On; 4x MSAA On (Apply graphics); cockpit screen fix On. Restart between | Each still logs `Motion source now native renderer`. FG shows generated frames with no new warping or black frames. If the log shows `Native renderer motion unavailable`, report the reason it gives |
| 11 | Motion-quality capture: set `XPFSR24_MOTION_CAPTURE` to an empty folder, pan steadily, once with Camera and once with Native | `python tools/motion_quality.py <folder>` prints a gain for each region. Report both outputs. Clear the variable afterwards |
| 12 | Motion survey (stage E groundwork): set `XPFSR24_MOTION_SURVEY=1`, fly 1 minute (cockpit + external, near traffic, props turning), then clear it | The log has `Motion survey:` lines and a `Motion survey summary:` line. Attach those lines |
| 13 | Live-test fixes: (a) pin a help card, press Esc; (b) 2 minutes of straight airborne flight with FSR + FG; (c) the same 30 s ground pan with *FSR FG 4.0.2* and with *FSR FG 4.0.2 synchronized* | (a) The card closes and X-Plane's menu does not open; other keys still reach X-Plane. (b) Camera resets are rare in cruise (report the count, as in the last test). (c) Report source FPS for each and the log's `sync-drain-avg-ms` |
| 14 | Diagnostics > **PERFORMANCE PER FRAME** in the same scene with: FSR only; FSR + FG (SDK paced); FSR + FG synchronized | *This mod (CPU)* and *X-Plane* lines show numbers within a few seconds. Report all four mod numbers and X-Plane's frame and GPU times for each. The log's `per-frame-us:` line should match |
| 15 | Diagnostics > **Driver pacing** with frame generation On | Shows the same reasons as the log's `Driver-assisted pacing check` line, or "Requirements look met". Turning VSync on and restarting adds "turn VSync off" |

5. Collect `Output/FSR-Logs` (Diagnostics > Open log folder) and X-Plane's `Log.txt`.

## Report template

```
Build: test-<version>.<n>   Commit: <sha>
System: GPU + driver, CPU, RAM, Windows version, monitor resolution + refresh, VRR on/off
X-Plane: version, aircraft, airport, weather
Results: 1 PASS/FAIL ... 13 PASS/FAIL (one line each, with numbers where asked)
Motion quality: Camera gains (sky/aircraft/near/far) vs Native gains
Problems: what you saw, when, and how to repeat it
Logs: attached / link
Verdict: PROMOTE TO BETAS  or  NEEDS FIX
```

A **PROMOTE TO BETAS** verdict moves the change to the `beta` branch. Promotion
from Betas to Release Candidates still needs the checklist in
[RELEASING.md](RELEASING.md#release-candidates-and-betas-v2-onward).
