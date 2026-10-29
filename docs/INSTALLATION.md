# Install and inspect FSR Ghosting Fix

Use the complete release package or build `app/` as described in BUILDING.md.
Close X-Plane and the previous launcher before installing/updating anything.

## Required simulator configuration

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

Change graphics only while X-Plane is closed. Select a preset, or use Advanced
graphics > Apply and save, then relaunch. If the setup report and live renderer
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

For normal Native AA use Full internal resolution, MSAA Off and FXAA Off.
Upscaling needs a lower internal scale and the desired **output** resolution.
Manual MSAA/FXAA are available for native-simulator launches. Presets support one
X-Plane monitor on the primary display and show only advertised output sizes.

All launcher choices save automatically to `settings.ini` beside the app. The
saved preset is checked/reapplied with backups before the next FSR launch, so a
simulator preference reset does not silently discard it. Custom manual changes
use **Advanced graphics > Apply and save**. Unsaved manual edits are explicitly
marked pending. The title area reports save failures rather than claiming success.
Old launchers saved only the folder; choose a preset once after upgrading.
Keep the app in a writable folder. Click or hover a **?** for what a setting does
and its best use. Enabling FG turns cockpit restore/tint Off; enabling restore
or tint turns FG Off. Selecting a preset also sets FG Off.

The **Live simulator status** panel distinguishes NOT RUNNING, STARTING, VULKAN
LAYER LOADED, FSR ACTIVE, PAUSED and NO RECENT ACTIVITY. Green **FSR ACTIVE** means
recent FSR command recordings plus Vulkan presentation telemetry, not proof of
visual quality. Loading a flight is usually needed. A camera-plugin mapping by
itself never proves injection. A one-time Windows tray notification is requested
on first active processing; Windows can suppress notifications. Status stays in
the launcher. Closing/reopening the launcher loses monitoring of an already
running simulator; launch a new session from the app to monitor it.

Graphics changes require X-Plane closed. Exact files/change manifests are kept
in `graphics-backups/`. **Restore last change** restores only matching changed
keys; it refuses to overwrite later conflicting choices. Before uninstalling,
restore graphics if wanted, close X-Plane and use **Remove camera plugin**.
Then close the launcher and run the package uninstaller. Backups/logs are retained.

The existing FG path is available with cockpit restore Off. It remains experimental
and includes UI in interpolation. VR, multiple windows, multi-GPU and complete
object/cloud motion are unsupported. Read STATUS.md before contributing results.
