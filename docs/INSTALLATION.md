0.6.6: CHANNEL SWITCHING AND SETUP
Close X-Plane and GPU checks. Open Updates / Channel and select Normal or
Experimental. Wait for the check to finish, then use the enabled install button.

An app installed with official setup uses Switch and install. An extracted copy
or a copy missing its installation marker uses Switch using setup... instead.
It downloads and verifies the official installer, then opens the setup wizard.
Choose an empty application folder or an existing official installation folder.
Do not choose the portable folder if setup says it is unrecognized/nonempty.
New installations import the current saved choices; an existing installation
keeps its own settings. The source copy and its choices are left intact.
Open the newly installed app from the Start menu or the selected install folder.
Choosing a channel alone does not replace the installed build or enable FG.

Normal keeps built-in FG Off. Experimental includes FG testing options with
known flicker, artifacts and pacing limitations. This patch changes the updater
and setup, not rendering or interpolation algorithms. Linux/Mac previews are unchanged.

The captioned, narrated 54-second 0.6.4 channel/display guide is reused unchanged.
Its on-screen version remains 0.6.4. Current setup behavior is explained here.
Skip / Continue, Escape or Close returns immediately. Replay from What's new;
pause, mute and opt out of future videos in the player. Media plays offline.
Missing media does not block the launcher. Keep all media credits and notices.

# Install FSR 3/4 + Frame Gen for X-Plane

Built-in frame generation remains temporarily suspended in regular releases.
The disabled selector shows **Off — Temporarily suspended**; its help explains
why. Old settings and environment variables cannot enable it. FSR 3/4 Native AA
and upscaling still work. Keep using the same application folder when updating;
your other choices are preserved. Previous FG choices are retained when saving
unrelated settings, but this build launches with FG Off.

Fresh regular installers omit FrameGenCheck.exe and the AMD FG runtime. Returning from Experimental through setup removes the two owned FG components;
this Normal launcher/layer do not load or enable that path. FG remains development-only on the separate experimental
branch and should not be used as a remedy for unstable base frame times.

0.6.0 introduces a redesigned charcoal/champagne launcher. Presets, AA, input
scale and output controls remain on the main screen; resize or scroll when
needed. Launch actions and live status stay in the footer. Your existing saved
choices and installer identity are retained. Click **Platform support** for the
audit: this download is **Windows x64 only**. Separate native preview downloads provide Linux FSR 3.1 and Mac diagnostics only. Live-flight compatibility is not confirmed.

0.5.9 adds fresh session logs, rejects stale camera/depth frames and adds a
ground-camera safeguard for external views. Close X-Plane before updating and
start it again from the new launcher so its recognized owned camera publisher
can be updated with a backup. No extra motion plugin is required. Complete
independent object/cloud motion and zero ghosting are not guaranteed.

Use the complete release package or build `app/` as described in BUILDING.md.
Close X-Plane and the previous launcher before installing/updating anything.

The app is **FSR 3/4 + Frame Gen for X-Plane**. The **FSR Ghosting Fix** label
refers only to the optional cockpit-screen feature. Its current aircraft support
is explained in that setting's help; renaming it does not broaden compatibility.

## App updates

Open **Updates / Channel** and choose **Normal** or **Experimental**. Opening
the dialog checks availability; **Check now** retries manually. Startup automatic
checks can be disabled independently. Channel selection and this preference are
remembered. Normal includes alpha releases; Experimental has development FG options.

For an official installation, **Download and install** or **Switch and install**
downloads setup, verifies its size and SHA-256 against GitHub's HTTPS metadata,
closes the launcher, installs into the recognized app folder and reopens it.
The selected simulator and saved choices are preserved. Close X-Plane and GPU checks.

For an extracted copy without a setup marker, **Install using setup...** or
**Switch using setup...** performs the same download verification, then opens
full setup so you can choose the target. It imports the current settings only
when the target has no settings.ini. Existing installation settings are never
replaced by an import. Unrecognized nonempty folders remain protected. Use the
newly installed launcher afterward; the portable source remains unchanged.

If the selected channel is already installed and current, the dialog says so;
choose the other channel to switch. Failed release checks can retry with Check
now. Failed downloads or installer handoffs can retry with the install button.
Changing channels while checking cancels the old check; an old result cannot
install the wrong build. The selector is locked during download/handoff.
There is no background service or mid-flight installation. No GitHub login is
needed; simulator paths, settings and logs are not sent to GitHub.

Offline, rate-limited, interrupted or invalid downloads show a retry message.
**Close / cancel download** cancels the download. Setup waits for launcher exit
and rejects files that are in use. If setup fails after installation starts, use
the full release installer in the same folder to repair the app; retain a working
release. The verified installer remains in a unique XPFSR24-update folder under
Windows Temp and can be removed after setup finishes.

SHA-256 verifies download integrity against GitHub, rather than an independent
publisher signature. The original launcher/installer is currently unsigned;
Windows may show its normal reputation prompt. Updates do not bypass it.
Release-feed and integrity handling follow the official
[GitHub Releases API](https://docs.github.com/en/rest/releases/releases) and
[WinHTTP options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags).

## Required simulator configuration

### Flickering, external-view artifacts or jagged edges

When the aircraft is on the ground and an external camera is within 1.5 metres
of terrain, **GROUND CAMERA SAFETY / Native rendering** temporarily bypasses
FSR and pauses frame generation. Your selections remain saved. Raise the camera
above 2.5 metres, switch to the cockpit, or take off to resume automatically
with fresh temporal history. The gap between the two heights prevents rapid
switching at the ground collision limit. This uses read-only camera/ground
metadata; it does not move the camera, disable collision or touch controllers.
Rendering/AA and frame rate can change while the safeguard is active. It is a
precaution for the reported ground-level pan artifacts, not a verified cure for
every aircraft, cloud or view. It requires the updated bundled publisher.

Regular 0.6.2-alpha keeps FG disabled while these reports are investigated.
Close X-Plane before updating; the launcher updates its owned camera publisher
on the next FSR launch. If artifacts persist with FG Off, retain the renderer
log. FG testing belongs to the separate experimental branch; do not enable it
as a remedy for poor base frame times. Frame generation does not repair AA.
For airborne edge shimmer, compare Native AA at Full rendering scale against
Quality upscaling, with MSAA/FXAA Off. The fixes require live confirmation and do
not supply complete motion for clouds, propellers or all aircraft.
See [rendering stability and retest steps](RENDERING-STABILITY.md).

Click **Sim setup...** on the main screen for the checklist and a read-only
**Check saved settings** report. It highlights mismatched AA, internal scale,
saved output and monitor configuration. It does not change the simulator.

| Mode in launcher | X-Plane internal rendering scale | X-Plane MSAA | X-Plane FXAA |
| --- | --- | --- | --- |
| FSR 3/4 Native AA | Full resolution | Off | Off |
| FSR 3/4 Quality upscaling | Quality (1.5x ratio) | Off | Off |
| FSR 3/4 Performance upscaling | Performance (2x ratio) | Off | Off |

The simulator's Rendering Resolution / FSR scale sets the **input** size for
this integration. Do not force it to Full in upscaling mode just because its
label mentions FSR. The launcher selects the FSR 3/4 provider. **Output** is a
separate setting: 4K Quality uses 2560x1440 input and 3840x2160 fullscreen output.
Selecting a 1440p output never makes it 4K. Presets configure both together.

For the first general test use a display-supported **FSR 3 Native AA** preset,
**FG Off**, **cockpit-screen fix Off** and **alignment tint Off**. Use one simulator
window on the primary display, 2D with VR Off. Run AA check for the selected
provider, then **Launch X-Plane with FSR** and load a flight. Green **FSR ACTIVE**
and the reported actual input/output sizes confirm recent processing. Diagnostics
mode does not enable reconstruction. FSR 4 requires runtime-supported hardware
and drivers; use its AA check before a flight.

This is a Windows Vulkan build. X-Plane 12 already uses Vulkan on Windows; do
not look for the old X-Plane 11 Vulkan-enable checkbox. Other DLSS/TAA renderer
replacements, MotionVectors/LuminaXP plugins or a local `vulkan-1.dll` proxy must
be disabled for this integration's isolated configuration. General graphics
quality sliders can be tuned for performance; FG cannot repair slow base frames.
The setup report reads disk preferences; it does not verify every running add-on,
VR state, driver override or unsaved in-simulator change.

Change graphics only while X-Plane is closed. Select a preset, or use the main
screen's graphics controls and **Apply graphics**, then relaunch. If the setup report and live renderer
sizes differ, trust the live sizes and inspect the log before judging quality.

## Optional cockpit-screen check

1. Complete the general setup above and select the folder containing `X-Plane.exe`.
2. Run **AA check**, then **Ghosting check**. View the analytical
   cockpit comparison; it is a GPU regression fixture, not live-simulator proof.
3. Choose a **Resolution and FSR preset** on the main screen. Both FSR 3 and
   FSR 4 have Native AA, Quality and 4K Performance choices. `FSR 4 - 4K Quality`
   selects the 4.1.2 ML-preview provider, 2560x1440 input and 3840x2160 fullscreen
   output. Presets apply immediately and back up changes while X-Plane is closed.
   MSAA/FXAA and FG are set Off. Your choice is saved and restored when reopening.
4. Keep **FSR Ghosting Fix - cockpit screens** enabled and **frame generation Off**.
   Its current supported aircraft is the default Cirrus SR22; this naming update
   does not add other aircraft or fix scenery/cloud ghosting.
5. Enable **Green screen-alignment tint**, then **Launch X-Plane with FSR**
   with the default Cirrus SR22. Check that any green coverage is limited to the
   PFD/MFD screen interiors. An inactive status can mean geometry, stability or
   depth checks rejected the frame. Dispatch counts do not prove valid coverage.
6. If tint is misplaced or flickers, disable cockpit restore and report the
   exact aircraft/build/view/FOV and options. Do not treat a mismatched projection
   as a finished fix. Turn tint Off and restart only after alignment is verified.

This implementation restores the current rendered scene's screen pixels after
FSR, rather than sampling native avionics textures. It retains the frame's
lighting but is limited by input detail. Fast camera movement bypasses it to
limit cached-matrix misalignment. It is currently for the default SR22 only.

The camera publisher uses ABI 4. The launcher recognizes and backs up the owned
older ABI 3 publisher before replacing it, and refuses unknown existing builds.
Its Vulkan layer is scoped to the child process; no global Vulkan registration
or driver installation is performed. Other renderer mods/proxies must be disabled.

For Native AA use Full internal resolution and FXAA Off. Start with MSAA Off.
Upscaling needs a lower internal scale and the desired **output** resolution.
MSAA Off/2x/4x/8x is on the main screen and may be combined with FSR 3/4 as an experimental option. FXAA is available for native-simulator launches only. Presets support one
X-Plane monitor on the primary display and show only advertised output sizes.

All launcher choices save automatically to `settings.ini` beside the app. The
saved preset is checked/reapplied with backups before the next FSR launch, so a
simulator preference reset does not silently discard it. Custom manual changes
use the main-screen **MSAA**, **FXAA**, **Input scale** and **Output** controls,
then **Apply graphics** with X-Plane closed. Pending edits are remembered across
app restarts and applied at the next launch. Custom changes preserve the selected
FSR and frame-generation modes; incompatible FXAA or input-scale choices are
explained at launch instead of silently changing providers. The title area reports save failures rather than claiming success.
Old launchers saved only the folder; choose a preset once after upgrading.
Keep the app in a writable folder. Click or hover a **?** for what a setting does
and its best use. Enabling FG turns cockpit restore/tint Off; enabling restore
or tint turns FG Off. Selecting a preset also sets FG Off.

The **Live simulator status** panel distinguishes NOT RUNNING, STARTING, VULKAN
LAYER LOADED, FSR ACTIVE, GROUND CAMERA SAFETY, PAUSED and NO RECENT ACTIVITY. Green **FSR ACTIVE** means
recent FSR command recordings plus Vulkan presentation telemetry, not proof of
visual quality. Loading a flight is usually needed. A camera-plugin mapping by
itself never proves injection. A one-time Windows tray notification is requested
on first active processing; Windows can suppress notifications. Status stays in
the launcher. Closing/reopening the launcher loses monitoring of an already
running simulator; launch a new session from the app to monitor it.

## Session logs

Each new X-Plane launch from the app starts fresh `logs/renderer.log`,
`renderer-status.txt`, `exit-status.txt` and `launch.txt`. One previous session
is retained with the `.previous` suffix; stale temporary status is removed.
The new renderer/launch headers identify the version, session, process and UTC
start time. Opening the launcher again or reloading a flight does not wipe an
active session. If logs cannot reset, the app reports the error before starting
X-Plane. For a bug report, copy the current session's log before another launch
and review private paths before sharing it.

Graphics changes require X-Plane closed. Exact files/change manifests are kept
in `graphics-backups/`. **Restore last change** restores only matching changed
keys; it refuses to overwrite later conflicting choices. Before uninstalling,
restore graphics if wanted, close X-Plane and use **Remove camera plugin**.
Then close the launcher and run the package uninstaller. Backups/logs are retained.

Built-in FG is suspended. Development on the separate experimental branch includes UI in interpolation. VR, multiple windows, multi-GPU and complete
object/cloud motion are unsupported. Read STATUS.md before contributing results.


## Release channel and display behavior (0.6.4)

Open **Updates / Channel**, choose **Normal** or **Experimental**, then **Check
now**. Normal includes this project's alpha releases and keeps built-in frame
generation disabled. Experimental offers development builds with frame-generation
options. Choosing a channel only checks availability; **Switch and install** (recognized installation) or **Switch using setup...**
(extracted copy) downloads and verifies its installer with X-Plane and GPU checks closed. The
installed channel/version and selected download channel are shown separately.
The selection and rendering choices survive reopening and recognized updates.
Return to Normal through the same selector. A channel switch may deliberately
install an older build; ordinary updates within a channel cannot downgrade.

Experimental FG uses the existing SDK development implementation. Flickering,
artifacts and uneven pacing remain possible. It is not presented as fixed.
FG stays Off on a fresh installation; enable it only for testing with stable
base frame times. Cockpit-screen restoration requires FG Off. Selecting FG turns
restoration Off; changing provider needs a simulator restart. FG Off is available
during a flight. No Lossless Scaling or NVIDIA runtime/model is bundled.

Selecting a resolution preset or clicking **Apply graphics** deliberately applies
its output resolution. Later **Launch** actions preserve the simulator's current
output, window mode and accessibility preferences, while applying the remembered
AA/render-scale inputs. If you change output in X-Plane, the launcher leaves that
choice in place. Reselect the preset to apply its original output again. Pending
custom edits must be applied before launch. The preset name describes the saved
profile, not a guarantee of the current live output; check the active status size.

For menu readability, use X-Plane **Settings > Graphics > Accessibility > User
Interface Size / Base Font Size**. A deliberate output-resolution change can
change how large the interface appears. The mod does not force UI scale, edit
Windows scaling, change DPI compatibility overrides or restore old preferences
on simulator exit.
