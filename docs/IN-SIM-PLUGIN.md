# In-simulator controls and third-party launchers

Windows 0.7.2-alpha includes the in-sim controls preview. Full setup can install
and enable automatic activation for the selected simulator. The desktop app
remains an **optional secondary interface**, sharing saved choices and providing
updates, guide playback and complete diagnostics. This is not a claim of complete
in-sim parity or certification of Visual Engine/other launchers. Read
[the current installation instructions](INSTALLATION-0.7.2.txt).

## Development progress

- Original XPLM floating panel under Plugins, with Rendering, Neural rendering,
  Diagnostics and Image quality tabs. Toggles handle binary settings; sliders
  handle numeric NR adjustments; dropdowns handle modes/presets. The FG provider
  is remembered while its separate On/Off switch is Off. Model/IPC/layout tests
  cover all 20 NR fields, numeric limits, dropdown scrolling and persistence.
- Initial real XPLM drawing, help, scrolling, saved scope and live processing
  bypass/resume were exercised in the owner's LevelUp 737 session. The first
  window could overlap the top menu; the current build has an internal Close
  button and more title-bar clearance. The owner subsequently opened the new
  in-sim model selector, imported two verified models and enabled NR live.
  Current-session telemetry confirmed full-scene NR before upscaling while FSR
  continued. This is a functional check, not certification of NR image quality.
- A separate panel-presence mapping pauses generated frames while controls are
  visible, preserving the saved FG selection and resetting history on return.
  Actual GPU resume/composition still needs live verification.
- Shared settings transactions serialize cross-process writers, merge individual
  edits and retain unrelated INI sections. Desktop controls refresh panel edits.
- Automatic Vulkan activation now has exact selected-executable gating, a native
  disable switch, shared session setup and an inactive pass-through path. CPU
  tests cover configuration/registry ownership. Unrelated Vulkan resource/device
  calls passed two lifecycles through both explicit and real implicit discovery,
  without creating renderer logs. Direct launch reached live FSR on the owner's
  machine. Visual Engine itself remains unverified. Setup packages the helper/manifest,
  can enable integration, refreshes an existing opt-in during updates and removes
  its owned registration/recognized plugin on uninstall. Opt-outs are retained.
  Isolated tests cover ownership and install/update/uninstall payload behavior;
  they do not certify every external launcher or Windows registry context.
- Preset/custom AA/output changes can be queued in-sim. A background component
  waits for the selected simulator's normal exit, then applies supported graphics
  changes with a backup. Fixtures verify source PID/path/token, abnormal-exit
  refusal, provider retention and unchanged display geometry for Keep output.
- Pending graphics edits no longer need to block compatible current input: the
  running profile follows the actual simulator scale, and retains the saved
  future choices. A failed selected-host activation gets current-session bypass
  diagnostics; another session cannot erase the active session's log.
- The panel can recover the exact selected simulator's saved profile from this
  installation's registration when the renderer never supplied a startup link.
  Older optional desktop launches can recover their own profile from the runtime
  path. Recovery never scans arbitrary installs or claims an active renderer.
  An unlinked panel shows dedicated connection guidance instead of overlapping
  editable controls. A linked but inactive panel labels controls for next session
  and starts fresh plugin-only diagnostics under the shared log-ownership lease.
  Tests cover wrong-executable/disabled-install rejection, legacy recovery,
  disconnected layout, inactive telemetry and protection of another session's log.

  A normal Explorer launch reproduced a missing renderer and an unlinked panel.
  In-process diagnostics returned Windows error 2 for the installation owner
  key while setup-environment reads of the same registration succeeded. The
  settings file was intact. This establishes different registration visibility
  between those launch contexts; the Windows mechanism causing that difference
  has not been identified. Startup now logs lookup results and the specific
  rejection reason in X-Plane's own log, even without opening the panel.
  An unlinked panel does not claim settings were saved or show an empty scroll
  range. Profile recovery alone still does not establish renderer activity.

  Double-clicking `IntegrationSetup.exe` from the installed package provides a
  registration-only repair in the normal desktop user's context. The equivalent
  noninteractive operation is `--repair-registration`. It requires an already
  enabled profile, verified runtime and an installed plugin matching that exact
  package. It refuses another installation's owner or an opted-out profile,
  preserves user settings/models and never replaces a loaded plugin. The repair
  reports its result; restart X-Plane normally to activate the Vulkan layer.
  After this desktop-context repair, another Explorer launch enumerated the
  project's Vulkan layer and created a fresh renderer session. This verifies
  startup registration for that launch; in-flight image quality and third-party
  launcher compatibility remain separate checks.
- Neural rendering now has a model DLL selector backed by a separate native
  helper, bounded per-job progress and explicit success/cancel/failure messages.
  The supported user-supplied DLL is read by the pinned extractor, never loaded.
  Every extracted model is hash-checked before publication. First-time import
  may add missing model files during a flight; existing differing models cannot
  be replaced while running. Turn NR Off/On after success to retry preparation.
  No DLL/model weights are bundled. Desktop import shares the model writer lock.
  Local real-model fixtures also cover identical reimport without replacement,
  invalid second-output refusal and verified backups for offline replacement.
- Budget buttons highlight their current selection, acknowledge repeated clicks
  and distinguish saved choices from missing-model, Off and preparation states.
  Extra header spacing separates activity from tabs and settings.
- Cloud quality and Ultra Quality / Native AA cloud-clarity setups are being
  added to Image quality. They queue backed-up preference edits after exit,
  retain the chosen FSR family/FG/NR and leave the engine rendering table alone.
  See [findings and live-comparison limits](CLOUD-CLARITY.md).
- Complete diagnostics/log UI, updates and guide playback still
  require in-sim parity work before release instructions can remove the desktop
  requirement for all operations. Windows Vulkan is the first target.

## Capture-hook compatibility

The owner's first direct-launch FG test crashed in OBS's installed
`graphics-hook64.dll`. The desktop path sets OBS's documented disable variable
before the loader discovers layers. An implicit layer runs after that decision.
Changing the variable then cannot retroactively remove an already loaded hook.

Automatic activation therefore retains the saved FG choice but bypasses FG when
the OBS hook is already loaded, including a second check during device startup.
It leaves global OBS settings/registration untouched. Direct launch with this
guard reached live FSR on the R9700; full FG with OBS in the automatic path is
not certified. The optional desktop path remains available for the early
capture-compatibility environment. Report the bypass in status instead of
claiming generated frames. See [OBS's layer manifest](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/obs-vulkan64.json).

## Activation timing

`Resources/plugins/XPFSR24/64/win.xpl` publishes camera metadata using public
read-only XPLM datarefs and provides an enable/disable menu. The Vulkan layer
does the FSR/NR work. The setup-enabled implicit layer configures the renderer before device creation;
the optional desktop path can also configure it explicitly. Installing the XPLM
plugin alone does not activate the renderer for another launcher's process.

The tested X-Plane 12.4.4-r1 startup log lists Vulkan initialization before
loading XPFSR24 and other XPLM plugins. Required device features must be
requested at `vkCreateDevice`. Enabling the layer in `XPluginStart` is too late
for that session. The local public SDK exposes no native Vulkan device or
command-buffer integration for this purpose. Laminar's
[plugin drawing guidance](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/)
supports OpenGL plugin UI even when the simulator renders using Vulkan/Metal;
that bridge is not access to the simulator's native Vulkan reconstruction pass.

## Integration design

Keep the renderer and XPLM control panel as two cooperating components:

1. A separately enabled, per-user **implicit Vulkan layer** starts before
   X-Plane creates its device. Windows loader discovery supports a manifest
   value under `HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers`; this needs no
   replacement `vulkan-1.dll` and no modification of another launcher's files.
2. Activation must verify the exact canonical `X-Plane.exe` path selected by
   the user. A filename or application-name match alone is insufficient.
   Every other Vulkan application must follow an unchanged pass-through path,
   without feature additions, SDK loading, shader interception or session files.
3. The layer loads a validated saved profile and creates session IPC/status
   before device creation. A dedicated disable environment variable must
   bypass automatic activation, including for Native comparison.
4. An XPLM floating window under **Plugins > FSR 3/4 + Frame Gen** displays
   confirmed renderer status and adjusts supported controls through that IPC.
   It must distinguish 'plugin loaded' from 'FSR/NR recording'. NR On/Off,
   artistic controls, performance profiles and scene scope can remain live.
   FSR/FG provider selection requiring resource/device changes remains a
   next-session choice until safe recreation has been implemented and tested.
5. Setup/updates/uninstall own only this project's manifest and registration
   value. They must leave other layers, add-on preferences, launch arguments,
   controllers and aircraft files untouched. Removal must work when an external
   simulator drive is disconnected. Do not remove another installation's entry.

Use [XPLMCreateWindowEx](https://developer.x-plane.com/sdk/XPLMCreateWindowEx/)
for the control panel; draw only while it is visible. Preserve keyboard focus
and do not request camera or joystick ownership.

Khronos documents the implicit-layer mechanism, mandatory disable switch and
[Windows discovery](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md).
The newer per-application loader-settings mechanism is another possibility,
but must not replace a user's existing VkConfig or loader configuration.

## Compatibility checks required before shipping

Automatic startup matches Native AA versus upscaling to X-Plane's actual input
scale, preserving the selected FSR family and saved graphics/NR/FG choices.
This also covers input changes made outside our panel or after the pending flag
was cleared. A temporary running-mode adjustment is explained in the panel;
it does not rewrite simulator resolution or UI size. Select Full input and
Apply after exit to use a saved Native AA choice with full scene input.
FXAA and other incompatible renderer combinations still require correction.

If activation is refused, the panel retains the actual startup reason instead
of reporting every failure as a missing layer. Its wrapped status and guidance
have separate footer regions, including at the minimum supported window size.

- Launch X-Plane directly, via this launcher and via a supported third-party
  launcher; confirm the same selected installation and fresh renderer session.
- Test an external drive and a non-default install path, a disconnected drive,
  multiple X-Plane installs, native comparison, and two app versions installed.
- A separate Vulkan application must get no FSR/NR changes. Explicit plus
  implicit discovery must not load the layer twice.
- Handle a launcher that already runs in the background; it may not inherit
  the environment of the desktop app that opens it. Environment inheritance
  alone is therefore not a reliable universal solution.
- Verify multi-writer IPC and saved-setting synchronization between the
  desktop app and in-sim panel, process exit, plugin disable/re-enable, resize
  and device loss. Serialize NR writers and merge saved-setting changes before
  publishing them so disk and live controls agree.
- Visual Engine needs its own actual test. Add-on-specific toggles cannot
  establish rendering compatibility. Test cloud/exposure changes, overlays,
  screen restoration and FSR/NR/FG combinations before advertising support.

## Platform limits

Windows Vulkan is the first target. Linux uses loader manifest directories
instead of Windows registry keys and needs native renderer/IPC integration and
testing. X-Plane on macOS renders using Metal; a Vulkan layer or MoltenVK
manifest cannot turn this Windows Vulkan implementation into a Metal plugin.
An in-sim user interface alone does not change these renderer limitations.
