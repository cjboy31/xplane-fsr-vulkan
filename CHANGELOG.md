# Changes

## 2.0.0-beta.windows.1

- **Hover help:** point at any ? in the panel to read a short guide in place. Click to pin it; click again or press Esc to close. Help no longer takes you to a separate page.
- **Frame pacing:** new choice under Frame generation: Smooth (AMD default), Recover after heavy scenes (AMD tuning set B) or Power saver. Applies after restart. The session log explains when AMD's driver-assisted pacing (RX 9000 + Adrenalin 26.10.1+) can take over.
- **Easier install:** setup finds X-Plane 12 automatically and uses plain step-by-step wording.
- **Clearer channels:** Release Candidates (tested and verified) and Betas (still being tested). Your saved channel is kept.
- **Project renamed** to `xplane12-fsr-framegen`. Web links to the old name still open. The 0.7.x updater cannot follow the rename, so install 2.0 manually once; from 2.0 on, the updater accepts both names.
- Repository clean-up: retired feature-video tooling and old per-version install notes removed; developer notes moved to `docs/developer/`.
- Includes the 0.7.10 scene-copy and background file-worker performance corrections.
- Not yet verified in live flights; this is why it is a Beta.

## 0.7.10

- Skip image copies from passes that do not feed the final scene, reducing repeated graphics-memory setup and associated CPU spikes.
- Move status-file writes and live-control file checks to a background worker. Keep saved choices, fresh-session logs and live On/Off controls.
- Retain the current FSR, NR and frame-generation options, memory limits and completion safeguards. Performance gains vary; NR and FG still add processing work.
- Windows rendering correction. Linux keeps its separate FSR 3.1 preview; Mac rendering remains unavailable.

## 0.7.10-beta.windows.1

- The same tested performance correction on the Beta channel. Release is recommended.

## 0.7.9

- Share a filtered diagnostic bundle from Diagnostics to Discord #render-logs or #bug-reports. Preview first, then send now or once after this session. Reporting starts Off and can be canceled before exit.
- Include the matching X-Plane Log.txt, available setup errors, saved image settings summary and small same-session crash text. Keep original files unchanged. Models, DLLs, binary memory dumps and unrelated files are excluded.
- Show confirmed delivery receipts and clear errors; uncertain uploads are not repeated automatically.
- Ignore empty or disabled known-plugin leftovers in the reconstruction conflict check, while keeping protection for actual conflicting Windows plugin files. A truly clean-install warning still needs a fresh log to diagnose.
- Add simpler preset instructions and recommended hardware/settings guidance. No new rendering algorithm or FPS improvement is claimed.
- Windows in-sim automatic reporting; browser uploads also support Native AA logs on Linux and Mac. Linux FSR rendering remains the separate FSR 3.1 preview; Mac rendering remains unavailable.

## 0.7.9-beta.windows.1

- The same diagnostic reporting and setup guidance on the Beta channel. Release is recommended.

## 0.7.8

- Release includes the current NR, frame-generation and compatibility updates from Experimental.
- The updater now has only Release and Beta. Previous Normal and Experimental choices map to the matching new channel.
- Keep your saved image settings and local models when switching channels.
- Download one clearly named setup executable. The old Ghosting-Fix-named compatibility copy is no longer generated.
- Keep the installer ownership repair from 0.7.7 and the rendering/startup fixes from 0.7.6. No new rendering algorithm is added.

## 0.7.8-beta.windows.1

- The same current feature set and channel migration as 0.7.8, packaged on the Beta track for future testing. Release is recommended.

## 0.7.7-alpha

- Fix setup rejecting a verified previous plugin when the new installer uses a different app folder for the same simulator.
- Reuse the recognized installed application folder by default. An explicit destination is kept.
- Check activation ownership before changing simulator files. Edited, unrelated and different-simulator installations remain protected.
- Show the actual setup error and full log location instead of always telling users to close X-Plane.
- Keep the rendering, startup recovery and buffer-reuse improvements from 0.7.6. No new AA, NR or frame-generation algorithm is added.

## 0.7.7-experimental.1

- The same installer ownership and error-message corrections as 0.7.7-alpha, on the Experimental channel. Normal remains recommended.

## 0.7.6-alpha

- Reduce repeated image-buffer setup that could cause CPU spikes, including when frame generation was switched Off after launch.
- Reuse buffers only after the simulator and frame-generation presenter finish using them. Keep the memory safeguards and existing flicker/freeze corrections.
- Keep the grouped controls, transitions, timing labels and per-setting help from 0.7.5.

- Recover saved settings from a verified link beside the installed plugin when Windows points at an old installation.
- Add Repair startup link to the in-sim panel when the enabled installation needs startup repair. Restart is still required to load the renderer.
- Allow repair to replace a stale disabled registration for the same simulator; keep enabled or different-simulator installations protected.

- Start fresh setup with a blank simulator path; keep existing user installations.
- Remove Automatic from the image-processor menu. Choose Native AA, FSR or Original simulator.
- Prepare compatible input scale and FXAA settings with X-Plane closed, without changing MSAA, output size or UI size.
- Add Open log folder to Diagnostics and copy session logs to X-Plane / Output / FSR-Logs after exit.
- Delay repeated memory-allocation retries and retire completed old FG resources safely. Native rendering continues while memory is low.
- Wait for the initial scene to settle before preparing FSR. Frame generation and NR still have their own processing cost; this is not a guarantee that every hitch is gone.

## 0.7.6-experimental.1

- Includes the same buffer-reuse, startup recovery, input setup and accessible-log corrections as 0.7.6-alpha.
- Keeps the Experimental update channel. Normal remains recommended and includes all current FSR, NR and frame-generation features.

## 0.7.5-alpha

- The in-sim panel groups image processing, frame generation, and neural controls. Section headers show when changes apply. Each setting has its own help.
- Tabs and switches now ease into place. Turn off Windows animation effects for instant changes.
- Diagnostics opens the installation log folder and remains usable at the minimum window size. Frame counts and session status stay separate from saved choices.

## 0.7.4-alpha

- Rendering controls stay inside X-Plane. The desktop app is now a plugin updater for FSR and independent Native AA.
- Read Changelog in the in-sim panel instead of watching a startup video.
- Clearer setting names and shorter ? help explain what each choice does.
- Cockpit display controls use the name Cockpit screen protection. Supported-layout and frame-generation limits still apply.
- Update checks keep live in-sim choices, verify downloads, and refuse installation while X-Plane is open. Native AA updates also reject downgrades and unsafe archive paths.


## 0.7.3-alpha

- Add a shared renderer choice for installations using both FSR and independent
  Native AA 0.3.0. Choose Native AA, FSR/NR/frame generation, the original
  simulator or Automatic; only one renderer processes a session.
- Put the next-launch selector on Rendering in the in-sim panel. Switching
  requires a normal restart and retains both mods' quality and graphics choices.
  Explicit desktop FSR/native-comparison launches override only that launch.
- Make the inactive panel and fresh logs explain intentional renderer selection.
  Process-identity-tagged ownership removes initialization-order dependence in
  updated builds; older already-active FSR versions make AA remain inactive.
- Retain the existing NR/FG, UI scaling, startup, CPU/memory and update-channel
  fixes. Full regression, actual activation and coordinated Windows startup
  checks pass. This does not add FSR4/NR/FG Linux or macOS rendering support.
- Include a skippable narrated guide explaining renderer choices, preserved
  settings, status and the required restart. Native AA itself remains separate.

## 0.7.2-alpha

- Add the Windows in-sim controls preview under Plugins > FSR 3/4 + Frame Gen.
  Setup can install the panel and enable renderer activation for the exact
  selected X-Plane installation, including an external drive. Launch normally;
  the desktop interface remains available for updates, guide playback and full
  diagnostics. Third-party launcher combinations require their own testing.
- Share saved choices between both interfaces. Add live toggles, NR sliders,
  model import, selected-budget highlighting, help and action feedback. Queue
  presets, MSAA/FXAA, output and cloud-quality changes for normal simulator exit.
  Provider/device changes still require a new session. FG pauses while the panel
  is visible; automatic activation bypasses FG if OBS's capture hook is loaded.
- Fix normal-startup registration/profile recovery and inactive status. Include
  a registration repair shortcut, fresh-session diagnostics and separated panel
  notices. Follow actual simulator input scale without overwriting saved intent
  or forcing output resolution/UI size again at launch.
- Reduce CPU/driver allocation churn with bounded completed-resource reuse,
  depth descriptor rebinding and filtering of unrelated-size depth captures.
  Keep fence/recording ownership, native fallback and the existing asynchronous
  FG lifetime path. See docs/CPU-PERFORMANCE.md for measured scope and limits.
- Retire obsolete NR contexts/scratch and completed command resources. Add
  memory-headroom checks, bounded idle caches and automatic NR pause/retry under
  pressure. These are safeguards, not a cap on X-Plane's total VRAM usage.
- Preserve settings and user-supplied models during updates. Retire the previous
  version guide only when its recorded hash matches; same-version repair retains
  the current guide and edited files are left alone. Refresh an existing
  in-sim opt-in; retain opt-outs. Uninstall removes only owned registration and
  recognized plugin files, retaining unrelated simulator files and local models.
- Validate the CPU/native-UI suite, production-code Vulkan reuse/OOM fixtures,
  ordinary startup and a stationary FSR-only comparison on the owner's R9700.
  Moving-view NR/FG quality and NR multi-pass ground blur are not certified;
  use one pass as the starting point. This release adds no multi-GPU offload,
  Mac Metal renderer or Linux feature parity.
- Reuse the previously approved 0.7.1 video unchanged. Current setup is described
  in the included instructions; no new film or before/after comparison is claimed.

## 0.7.1-alpha

- Fit the live log viewer and its minimum size to the owner's monitor work area;
  wrap footer controls on narrow/high-DPI displays. Cover small/secondary monitor
  sizes and footer containment; caught by the first hosted Windows CI run.

- Ship the current NR, FG and synchronized compatibility options in both
  Windows channels. Normal is the recommended full-feature release; channel
  selection controls future updates rather than access to these features.
  NR and FG remain Off on first use; model/hardware checks and explicit NR + FG
  opt-in remain. No NVIDIA DLL or model weights are included.
- Preserve display-encoded SDR color through FG when X-Plane uses an sRGB
  backbuffer. Private UNORM images avoid decoding the source before interpolation
  and then copying a darker result directly to present. A regression reproduces
  dark/bright levels 3/164 instead of 31/210 on the old path; the adapter preserves
  31/210 with both generation providers. Actual HDR/10-bit formats are unchanged;
  no full HDR compatibility claim is added.
- Configure the matching frame before PrepareV2 and suppress redundant
  configuration. Retain source-submission validation before Present.
- Add optional synchronized compatibility tests for FSR 3/4, requesting the
  official 3.1.7 presenter and draining its paced work after a generated present.
  These trade overlap/performance for stricter synchronization and are optional
  tests in both channels. Ordinary modes retain automatic presenter selection.
- Add opt-in local real/generated pixel diagnostics without per-frame GPU waits,
  and `--launch-fsr` to launch a saved profile through the normal validation path.
- Add a saved, live NR Processing order selector: Before upscaling is the
  recommended default; After upscaling processes output-resolution color.
  Both precede supported cockpit-screen restoration and optional FG.
- Use matching background contexts, stage-specific scratch images and history
  resets; keep valid FSR input/output when NR prepares, bypasses or fails.
  Show recorded order and dimensions in live NR status.
- Add a read-only live renderer log with automatic refresh, Follow latest,
  current/last-session labels and a bounded tail. Retain fresh-session rotation
  and preserve current evidence across renderer reloads.
- Add UTC/PID log timestamps, periodic renderer heartbeats and instance/device
  lifecycle evidence. Distinguish bypass/wait states from a layer unload.
- Base activity on advancing FSR recordings even when another pass bypasses.
  Retain prior layer confirmation during stale telemetry; reject fallback status
  from an old/wrong process, session or timestamp.
- Verify both orders, repeated live switches, Off and dirty-scratch bypass/error
  recovery in 48 real Vulkan/FSR 3/4 fixture phases. Pass a real-model HDR GPU
  check, saved-choice/migration checks and native UI/log refresh regressions.
  After-mode live-flight quality remains unverified; no artifact/FPS guarantee.
- Defer optional NR/FG until at least three seconds and 30 coherent initial
  source frames; prepare NR at lower CPU priority and allocate only the selected
  pass budget. Record first-use FSR initialization timings for diagnosis.
- Hold the complete real frame when a generation callback lacks matching or
  unconsumed source metadata. Report zero generated outputs on callback/SDK
  failure, reset history for recovery and retain synchronous SDK pacing.
- Test injected missing-source gaps on both FG providers: 90 real frames,
  87 non-black generated readbacks and three safe holds per provider. This
  validates the fallback path. The owner also confirmed that the encoded-color
  correction stopped visible flicker in an R9700 live NR + FSR 4 + FG session;
  broader hardware, display and long-flight quality remain unverified.
- Add an isolated opt-in async FG fixture and A/B/A timing harness. Both async
  providers pass the same gap/readback checks; this small workload shows no
  meaningful throughput gain. Application scheduling remains synchronized.
- Include a skippable, narrated feature guide for the updated features.
  NVIDIA DLLs/model weights and test-only backends remain excluded.

## 0.7.1-experimental.1

- Includes the same current feature set as Normal 0.7.1: independent NR with
  live On/Off, scope, budgets and Before/After upscaling order; FSR 3/4; frame
  generation with corrected sRGB display encoding; and optional synchronized
  compatibility pacing for both FG providers.
- Includes initial NR/FG startup deferral, selected-pass allocations, current
  session logs, clearer layer status and safe real-frame holds on missing input.
- Normal is now the recommended download for all current features. Experimental
  remains a separate future development update track. Hardware/model checks,
  NR + FG opt-in, saved selections and user-owned model/cache preservation remain.
- The owner confirmed the color correction stopped the reported live flicker
  with NR + FSR 4 + FG on an R9700. Broader display/hardware and long-flight
  testing remain open; neither channel is a universal artifact/FPS guarantee.
- No NVIDIA DLLs or neural model weights are included. Both installers include
  the matching skippable feature guide and plain installation instructions.

## 0.7.0-experimental.1

Experimental NR preview with the owner-approved feature guide.

- Add optional independent neural rendering before FSR on a private linear
  scene-color image, retaining original FSR input on bypass.
- Add saved Off/Subtle/Balanced/Strong/Custom controls, verified local model import,
  a separate-process GPU check and fresh per-session recording/pause reasons.
- Make NR On/Off, artistic controls, style, history, passes and scene scope live.
  Prepare model/resolution changes in the background; retain contexts until
  their command buffers can safely release them. Add 33/50/67% budget profiles.
- Store NR preferences independently so older Normal builds cannot erase them
  when saving their own settings. Migrate earlier prototype NR choices.
- Default to External only; offer Full scene for cockpit and exterior NR.
  Ground-camera safety remains active and temporal histories reset on changes.
  NR + FG requires an explicit experimental opt-in; FG provider changes still
  need a new simulator session. No artifact-free combination is claimed.
- Preserve proportional simulator UI size on supported single-display output
  changes, with opt-out and exact backed-up accessibility preference restoration.
- Fix launcher scrolling fragments and broken status/diagnostics repainting.
  Move controls in batches, repaint descendants in a buffered view and make
  wheel/scrollbar input at either limit a no-op. Retain partial wheel deltas.
  Correct runtime locking of controls after reparenting into the scroll view.
- Pair frame-generation callback reset metadata with its source frame rather
  than mutable latest-frame state; retain SDK pacing and synchronous defaults.
- Enable required Vulkan features without duplicate aggregate aliases; support
  core 1.4 pipeline robustness without a missing extension name.
- Compile pinned MIT shaders through the driver. No ACO binaries, loader
  proxies, NVIDIA DLLs or model weights are included in source/downloads.
- Pass live-control HDR fixtures and reduced-copy/copy-path output equivalence
  on the R9700. Synthetic GPU timings are NR-pass measurements, not simulator
  FPS or live-flight visual-quality guarantees.
- Exercise live NR On/Off, background model budgets and full-scene cockpit
  scope in X-Plane 12.4.4-r1. Also run NR with FSR 4.0.2 FG, retaining live NR
  bypass/re-enable. Both simulator sessions exit normally. Keep the combination
  experimental; these checks do not establish artifact-free flight quality.
- Document in-sim/third-party-launcher activation design and AMD denoiser input
  and backend requirements. These investigations do not add an implicit layer,
  Visual Engine certification or an unsupported denoising toggle.

## 0.6.7-alpha

- Fix stale text and button fragments when scrolling the launcher, including
  the live status and diagnostics cards. Move controls together and repaint
  the complete buffered view, including partially clipped native controls.
- Stop redundant movement/repainting at either scroll limit; retain partial
  wheel deltas from precision mice. Keep the launch footer fixed.
- Fix simulator-session locking of controls in the scroll view. Preserve saved
  choices. Add real native UI regressions at 96/120/144/192 DPI in two layouts.
- Normal continues to keep FG Off. NR development is separate. This maintenance
  release reuses the approved 0.6.4 guide unchanged; rendering is unchanged.

## 0.6.7-experimental.1

- Include the same scrolling, status/diagnostics repaint and runtime control
  fixes with DPI, clipping and repeated-edge-input regression coverage.
- Retain existing experimental FG options and limitations. This is a launcher
  maintenance patch, with no new NR, interpolation or motion-vector algorithm.
- Reuse the approved 0.6.4 guide unchanged; settings remain preserved.

## 0.6.6-alpha

- Fix a disabled install action when a release is available but the launcher
  is running from an extracted copy or has no recognized setup marker. Offer
  **Switch using setup...** instead: verify the download, open full setup and
  let the user choose an empty or recognized installation folder. Import the
  current choices only into a new installation; keep existing settings intact.
- Keep the automatic **Switch and install** route for recognized installations.
  Recheck ownership before handoff. Never silently overwrite an unknown folder.
- Cancel stale release checks when channels change, refresh on worker completion,
  enable retries after download/handoff failure, clean cancelled/retried staging,
  and explain when the selected channel is already installed. Center the updater
  on the launcher's monitor and check on its first opening.
- Add full dialog/worker/verification/handoff regression tests for both builds,
  plus setup migration, invalid-import and saved-choice preservation checks.
  Keep the dropdown drawing/DPI checks. Correct channel-specific setup wording.
- Normal continues to enforce FG Off. Rendering algorithms are unchanged.
  Reuse the owner-approved 0.6.4 guide byte-for-byte with reuse disclosed.

## 0.6.6-experimental.1

- Include the same actionable channel switching, verified setup fallback,
  saved-choice migration, retry/cancellation fixes and full-flow tests.
- Correct Experimental setup wording to describe its included FG testing options.
  Existing FG artifacts/flicker and pacing limitations remain; no interpolation,
  motion-vector or rendering algorithm changes are introduced.
- Bundle the owner-approved 0.6.4 guide unchanged, with reuse disclosed.

## 0.6.5-alpha

- Fix the blank release-channel dropdown by forwarding the updater window's
  owner-draw and item-measure messages to the shared theme renderer. Both Normal
  and Experimental choices are now painted, including selection highlighting.
- Verify the real updater window procedure against bitmap output at 100%, 125%,
  150% and 200% display scaling, in both Normal and Experimental build variants.
  Check row labels, background/text pixels, DPI height and persisted selection.
  The regression fails on the previous missing draw-handler implementation.
- Keep update/channel behavior, saved choices and renderer policies unchanged.
  Normal still enforces FG Off. This maintenance patch includes the already
  approved 0.6.4 channel/display video byte-for-byte, with explicit reuse
  provenance and a matching package-version manifest; no film revision.

## 0.6.5-experimental.1

- Include the same release-channel dropdown drawing/measurement correction and
  scaling regression checks in the separate frame-generation development build.
- Preserve FG options and their existing experimental limitations. No renderer,
  frame-generation or motion-vector algorithm changes are introduced.
- Bundle the previously approved 0.6.4 guide unchanged, with reuse disclosed.

## 0.6.4-alpha

- Replace the ambiguous preview checkbox with a saved **Normal / Experimental**
  release selector under **Updates / Channel**. Normal includes the project's
  alpha/beta/RC releases; the Experimental channel accepts `experimental.N`
  tags that the previous updater rejected.
- Show the installed channel separately from the selected download channel.
  Require an explicit **Switch and install** action for channel changes,
  including switching to a numerically older build. Same-channel updates never
  downgrade or repeatedly reinstall the current version. Preserve user choices.
- Provide a separately compiled Experimental frame-generation build with both
  SDK FG options and diagnostics. Normal retains the enforced FG suspension and
  excludes the FG runtime. This exposes existing development code; no new FG
  algorithm, artifact cure, pacing improvement or flight-quality claim is made.
- Keep the simulator's current output resolution and window mode when launching
  with a saved profile, including native comparison. Apply output only on a
  deliberate preset selection or **Apply graphics**. Keep pending custom edits
  pending until applied. UI scale/font remain under X-Plane's control.
- Add repeated-launch display-preservation, channel discovery, switch-back,
  persistence and installer channel-transition regression coverage. Include a
  skippable guide to the two channels and the revised display workflow.

The display policy removes a repeated launcher override. The report of UI
resizing at the same preset still needs a matched live reproduction; do not
claim every Windows/X-Plane DPI issue is resolved. Experimental FG retains the
previously reported flicker/artifact limitations. Linux/Mac previews unchanged.

## 0.6.4-experimental.1

- Separate opt-in Windows frame-generation development package based on the
  current FSR 3/4 camera/depth correction, with the saved Normal / Experimental
  channel selector and deliberate display-setting behavior from 0.6.4-alpha.
- Enable the existing FSR 3.1.6 and 4.0.2 FG options and include their GPU check
  plus the unmodified, signed AMD SDK frame-generation runtime. Fresh settings
  default FG Off. Cockpit-screen restoration requires FG Off.
- Allow an explicit return to Normal through Updates / Channel. Recognized
  channel switches preserve saved choices; the Normal installer removes the
  two owned FG payload files and enforces FG Off.
- Verify both FG providers with synthetic moving-image GPU readback, additional
  SDK presents and swapchain recreation/retirement tests. These do not establish
  in-flight quality, latency or scanout pacing. No new generation algorithm,
  artifact fix or Lossless Scaling-equivalent quality is claimed. The older
  native-motion experiment remains separate.
- Include the same skippable channel/display feature guide. This package remains
  prerelease; reported FG flicker/artifact limitations have not been resolved.

## 0.6.3-alpha

- Improve FSR 3/4 camera-motion reconstruction by normalizing X-Plane's plugin
  projection to the native Vulkan depth convention before computing world/body
  motion, scene distances and FSR depth parameters. Already-native projections
  pass through once; unsupported matrices safely bypass reconstruction.
- Reject an incompatible older camera publisher and explain that the plugin
  needs updating and X-Plane restarting. The launcher backs up and replaces
  recognized owned publishers; other plugins are not overwritten.
- Validate the correction in X-Plane 12.4.4 with repeated external yaw/lateral
  sweeps, ground-camera transitions and a powered cockpit. Confirm FSR 4 Quality
  reconstructing 2560x1440 to 3840x2160 with the 2x MSAA option selected. Add
  projection regression tests and GPU fallback/recovery coverage for both FSR
  providers. See docs/ARTIFACT-INVESTIGATION.md for scope and evidence.
- Replace clipped HDR comparison sources with genuine FP16 captures and the
  same measured HDR-to-SDR conversion on both sides of the feature guide.
  Keep Skip, replay and existing saved-choice/update behavior.

This is a Windows Vulkan correction, not a universal ghosting cure. Native
rendering still takes over at external ground contact. Independently animated
clouds/objects and other aircraft/add-ons need further validation. Built-in FG
stays suspended; Linux/Mac previews are unchanged. No new GPU passes or runtime
readbacks are added by this correction.

## 0.6.2-alpha

- Add a captioned, locally narrated setup/feature video with a fresh matched
  X-Plane 12.4.4 / FSR 3.1.6 Native AA capture. Clearly label the comparison's
  1080p settings, sequential captures and experimental limits; no FPS or general
  ghosting-fix claim. The renderer algorithms/providers are unchanged.
- Show the verified bundled guide once per installed version. Add immediate
  Skip/Continue, Escape/Close, replay from What's new, Pause and Mute. Remember
  the user's automatic-video/mute choices independently of rendering settings.
- Load the offline Windows decoder only for playback; missing optional media
  components or corrupt/missing media do not prevent normal launcher use.
- Require matching versioned video, transcript, captions, credits and provenance
  during Windows packaging. Preserve settings through updates. Native Linux/Mac
  preview packages are unchanged; Mac remains diagnostics only. Regular FG stays
  suspended and NR stays canceled.

## 0.6.1-alpha

- Temporarily suspend built-in frame generation in regular releases. Show
  **Off â€” Temporarily suspended**, disable its diagnostic action, and explain
  the suspension through the setting's help. Keep the existing application name.
- Force FG Off at launch and in the Vulkan layer, including old saved choices,
  inherited environment variables and stale FG-control files. The public layer
  uses native presentation and does not create the SDK frame-generation swapchain.
- Exclude the frame-generation runtime and diagnostic executable from fresh
  regular installation packages. Keep FSR 3/4 AA/upscaling, other saved choices,
  cockpit restoration and the ground-camera safeguard. Retain the old FG choice
  in settings for experimental use when saving unrelated settings.
- Keep FG development and its existing release on the separate experimental
  branch. Document the source/pacing/UI work and live validation needed before
  bringing frame generation back, with links to Lossless Scaling's public design
  notes. No Lossless Scaling code, models or binaries are incorporated.

This suspends the reported problematic path; it is not a new interpolation
algorithm or a claim to fix other sources of ghosting/stutter. Existing owned
installations can retain unused files from older releases; the new regular
launcher and layer do not enable them.

## 0.6.0-alpha

- Redesign the native Windows launcher with a charcoal palette, champagne
  accents, rounded cards, clearer typography, custom dropdown/button/toggle
  styling and visible keyboard focus. Keep presets, FSR/FG, MSAA/FXAA, input
  scale and output settings on the main screen.
- Add responsive layout, vertical scrolling at smaller sizes, focus-driven
  scrolling and per-monitor DPI handling. Keep launch actions and live status
  visible in the footer. Annotate native field names for accessibility.
- Restyle help, simulator setup and update windows. Keep real injection states,
  ground-camera safety, saved-choice handling and update integrity checks.
- Add a Platform support action and an evidence-based platform audit: current
  packages are Windows x64 only. Linux is a porting target, not confirmed
  compatible; macOS needs a separate Metal renderer implementation.

The renderer algorithms, motion sources, bundled provider binaries and camera
publisher are unchanged. No Linux/Mac package or new visual-quality claim is
introduced. Existing installer/update identities and saved settings are retained.

## 0.5.9-alpha

- Start each newly launched X-Plane process with fresh renderer, status, exit
  and launch logs. Keep one `.previous` copy, remove stale temporary status,
  and include the build, session ID, process ID and UTC start time. Reopening
  the launcher or reloading a flight does not clear an active session's log.
- Add a ground-camera safeguard for external views while the aircraft is on
  the ground: at camera clearance of 1.5 m or less, temporarily bypass FSR and
  pause frame generation. Resume with a temporal-history reset above 2.5 m,
  inside the cockpit or after takeoff. Show **GROUND CAMERA SAFETY** in the app;
  preserve the user's FSR/FG choices. Camera collision and controls are untouched.
- Preserve the actual recorded camera frame on secondary rendering commands.
  Reject stale or conflicting camera/depth metadata instead of relabeling it
  as current. Add a regression that fails against the previous layer.
- Add 16 real Vulkan layer checks across FSR 3/4, Native AA/upscaling and
  single-sample/4x MSAA, plus log-rotation and ground-clearance hysteresis tests.
  Recognize and back up the previous owned publisher when installing the new
  read-only camera-clearance metadata publisher.

The ground-camera change is a precautionary mitigation for the 0.5.7 report;
the reporter's exact scene has not been reproduced. Native rendering near the
ground can look different and pauses generated frames. This does not supply
missing cloud/object motion or guarantee that all artifacts are eliminated.
The alternative native-motion backend remains on its separate experimental
branch and update channel.

## 0.5.8-alpha

- Reduce motion packing to one selected world/body reprojection. Reconstruct
  eye position only inside the cockpit, using a squared-distance test without
  a per-pixel square root/divide. Preserve the FSR current-to-previous convention.
- Cache unchanged projection inverses; factor perspective and affine view/body
  transforms in double precision. Remove the unused aircraft-sphere dataref reads.
- Reject invalid internal-view eye transforms instead of retaining history with
  an unusable camera matrix. Keep existing view-cut and invalid-motion guards.
- Add independent perspective scene-flow tests for pan/rotation, zoom, body/world
  selection, normal/reverse Z and viewport orientation, plus repeatable GPU timing
  and large-world camera-math tests. Preserve ABI 4 and owned-plugin upgrades.

On the development R9700, five alternating-order paired runs measured about
8-15% less GPU packing time (1440p/4K, single-sample/4x MSAA, exterior/interior).
At 4K without MSAA the pass measured about 0.067/0.083 ms exterior/interior.
These synthetic pass timings are not a flight FPS gain. Complete moving-object,
propeller, cloud and transparency vectors remain unavailable; see docs/MOTION-VECTORS.md.

## 0.5.7-alpha

- Move MSAA, FXAA, input scale, output resolution and Apply graphics onto the
  main screen. Remove the Advanced graphics window.
- Keep FSR and frame-generation selections when switching to Custom, editing,
  applying or restoring graphics. Remember pending edits across app restarts.
- Add experimental 2x/4x/8x MSAA depth capture and nearest-depth resolve for FSR
  3/4. Track dynamic-rendering color resolves and explicit resolve commands.
  The SDK still receives single-sample inputs. Unsupported captures bypass FSR
  and suggest MSAA Off. FXAA remains native-only and never silently resets FSR/FG.
- Add per-sample GPU resolve checks in both depth conventions, MSAA layer
  integration scenes, and custom-edit/provider persistence regressions.

MSAA can increase GPU cost and does not fix cloud/texture aliasing. Synthetic
checks do not prove live-flight quality or the full MSAA + FG presentation path.
Use MSAA Off for the first test; try 2x next and check live FSR ACTIVE status.

## 0.5.6-alpha

- Serialize SDK frame preparation, presentation and game-queue submissions to
  remove a frame-generation race. Flush generated work before context recreation.
- Match interpolation to the submitted source scene, skip mismatched/run-ahead
  frames and prevent one prepared frame from being generated twice.
- Remove the external-view 20-metre aircraft history mask, which also covered
  runway and terrain. Retain more cockpit/sky history for temporal antialiasing.
- Reject invalid depth/motion samples and reset history on camera cuts, abrupt
  zooms, view/projection changes and teleports. Add periodic view/depth diagnostics.
- Add GPU packing regressions and generated-frame readback checks for both FG
  providers in UNORM and sRGB formats, including the format in the reported log.
- Recognize and back up the previous published camera plugin during upgrade;
  verify migration without starting or changing a real simulator installation.

These address concrete stability/history defects. The reporter confirmed that
turning FG Off stops the black flicker; the fixes still require their live-flight
retest. Complete object/cloud motion and all ghosting/jagged edges are not solved.

## 0.5.5-alpha

- Name the application FSR 3/4 + Frame Gen for X-Plane. Update launcher titles,
  tray text, executable metadata, setup, shortcuts, guide and release branding.
- Keep FSR Ghosting Fix as the cockpit-screen feature label, with its existing
  default Cirrus SR22 support and frame-generation-Off requirement explained in help.
- Prefer the newly named installer in the updater. Publish an identical legacy
  asset for 0.5.4 users so automatic upgrades continue to work.

Rendering and saved setting/installation identities are unchanged.

## 0.5.4-alpha

- Add automatic GitHub Release checks on startup and an Updates window with
  remembered automatic-check and preview-channel choices.
- Offer Download and install after a newer compatible release is found. Verify
  HTTPS origin, file size and GitHub's SHA-256 digest before running setup.
- Update the recognized installation in place, wait for the old launcher to exit,
  refuse locked application files and reopen the launcher with saved choices.
- Publish a standalone installer alongside the RAR for future in-app updates.
  Users of older versions must install this updater-enabled version once.

Installation requires the user's click and X-Plane/GPU checks closed. There is
no background service, forced mid-flight update or renderer change in this build.

## 0.5.3-alpha

- Add Sim setup on the main screen with a first-flight checklist and an explicit
  Native AA / Quality / Performance settings table.
- Explain the difference between simulator input scale, FSR provider and final
  output resolution, including 1440p input to 4K output.
- Add a read-only saved-settings report for AA, scale, output and monitor
  mismatches; direct users to presets/Apply and save while X-Plane is closed.
- Expand installation instructions with first-test options and live-status
  verification. Explain that X-Plane 12 already uses Vulkan on Windows.

Launcher/documentation update only. Renderer and camera publisher behavior are
unchanged; no new camera/controller fix or live-flight quality result is claimed.

## 0.5.2-alpha

- Rename the app to FSR Ghosting Fix for X-Plane and the cockpit option to
  FSR Ghosting Fix - cockpit screens. Use the same naming in the guide, metadata
  and diagnostics messages.
- Call the separate compatibility option FSR 3 history tuning to distinguish it
  from cockpit-screen restoration.
- Keep current aircraft support and FG-Off requirements explicit in help/docs.
  Only the default Cirrus SR22 cockpit-screen profile is currently supported.

Naming update only: rendering, settings keys, aircraft support and the saved
choices behavior are unchanged from 0.5.1. No new ghosting/stutter fix is claimed.

## 0.5.1-alpha

- Save launcher choices immediately and restore them on startup. Preserve legacy
  folder settings and graphics-backup metadata; report save failures explicitly.
- Move presets to the main screen and add seven matching FSR 4 ML-preview presets.
  Recheck/reapply saved graphics before launching FSR; keep selective backups.
- Add help beside settings, a wider two-column layout and automatic FG/cockpit
  compatibility adjustments.
- Add live injection states and request a one-time Windows notification only
  after recent FSR recordings and Vulkan presentation. Stale data is not active.
- Add persistence, status-transition and FSR 4 preset regression fixtures.

This release changes the launcher; it does not establish a live ghosting/stutter
fix or remove the SR22 compositor's FG-Off limitation.
