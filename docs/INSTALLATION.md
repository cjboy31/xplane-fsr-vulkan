FSR 3/4 + FRAME GEN FOR X-PLANE 12 — 0.7.8

START HERE - WHERE ARE THE PRESETS?
The presets are INSIDE X-PLANE. You do not open them by running a desktop EXE.
Install with X-Plane closed. Choose your picture settings with X-Plane running.
Then close X-Plane normally to apply any queued graphics changes.

STEP 1 - INSTALL (X-PLANE CLOSED)
1. Close X-Plane normally. Close the Plugin Updater too.
2. Download the recommended Windows Release. Beta is for testing newer changes.
3. Run the downloaded file ending in Setup.exe. If you downloaded the RAR,
   extract the whole RAR first and run XPlane-FSR24-Setup.exe inside it.
4. In setup, choose your own X-Plane folder: the one containing X-Plane.exe.
   It can be on an external drive. Turn on in-sim integration when offered.
5. Finish setup. Keep the existing application folder when updating so your
   saved choices and local NR models stay available.

STEP 2 - OPEN THE SETTINGS (X-PLANE RUNNING)
1. Start X-Plane using your usual shortcut or other simulator launcher.
2. Load a flight.
3. Open Plugins in X-Plane's top menu.
4. Choose FSR 3.1 / 4.1 + Frame Gen, then Open settings.
   Some older versions shorten the menu name to FSR 3/4 + Frame Gen.
5. Select the Rendering tab. Open the Resolution preset list and choose a preset.
   Custom means you will choose the separate settings yourself.

STEP 3 - APPLY A PRESET OR GRAPHICS CHANGE
1. In the same panel, open Image quality.
2. Scroll down to Apply graphics after I close X-Plane, then select that button.
3. Close X-Plane normally. Do not end it in Task Manager: queued settings need
   a normal exit. The background component applies the changes after exit.
4. Start X-Plane again and load your flight.
5. Open the panel and check for FSR ACTIVE. If you see Waiting or Bypassed,
   read the message. Selecting a preset alone does not prove FSR is running.

The preset is a starting setup. It turns MSAA, FXAA and frame generation Off.
Check the NR switch and leave it Off for your first test too. You can change
individual settings afterward. Use the ? buttons for help.

WHICH EXE SHOULD I RUN?
- The file ending in Setup.exe: installs or updates the mod.
- XPlaneFSR24.exe / X-Plane Plugin Updater: checks and installs plugin updates.
- X-Plane.exe: starts the simulator as usual.
- The other EXEs: support and test tools. Setup and the plugin use them when
  needed. Opening each one will not show the presets.

Full input + Native AA = smooth edges at your normal input resolution.
Quality input + Upscaling = draw a smaller picture, then build the output.
Keep FXAA, MSAA, frame generation and NR Off for the first comparison.
Select presets while the sim is RUNNING; apply queued graphics after EXIT.

YOUR SETTINGS ARE INSIDE X-PLANE
The old desktop rendering dashboard is retired. The separate X-Plane Plugin
Updater only checks and installs plugin updates. It can update FSR and Native AA.
Open it from Updates in the in-sim panel or from the Start menu. You may check
while flying, but must close X-Plane before installing. Downloads are verified.
Native AA updates also refuse downgrades. Your saved image settings are kept.

Open Changelog in the panel to read changes. No startup video is needed.
Select ? beside a setting for a short explanation and a starting point.
Group headings show when changes apply. The Windows Animation effects setting controls
the opening, tab and switch transitions. Diagnostics opens the FSR log folder.

FIRST SETUP
- Keep X-Plane FXAA Off when using FSR Native AA or upscaling.
- Native AA needs Full input detail. Upscaling needs a lower input level.
- Quality is a useful starting point for upscaling. At 4K output it uses a
  1440p input. Choosing 1440p output alone does not produce 4K output.
- Start with MSAA, frame generation and neural rendering Off. Add one effect
  at a time and compare the same view. MSAA with FSR is experimental.
- Choices marked live can change while you fly. Choices marked after restart
  need a new session. For graphics edits, choose Apply graphics after I close X-Plane,
  then quit normally and reopen it. Your current flight is not interrupted.
- Keep X-Plane UI size helps menus stay about the same size after output changes.

FRAME GENERATION
Adds pictures between real frames for smoother displayed movement. It does not
speed up the simulation. Begin with steady base performance. Try a compatibility
method if the normal method looks wrong. It pauses while the setup panel is open.

NEURAL RENDERING (NR)
Optional lighting, color and detail changes. It needs supported AMD RDNA4
hardware and your own supported model. NVIDIA DLLs and models are not supplied.
Open Neural rendering > Import NR model DLL and select your supported local
nvngx_dlssnr.dll (310.9.16 or 310.8.0). Wait for the import result.
Start with one pass, Balanced, and low effect strength. Extra passes can blur
ground textures. Before upscaling is the lower-cost starting point. Outside only
pauses NR in the cockpit; Whole scene includes it. Allow NR with frame generation
is a separate opt-in. If NR cannot prepare, FSR keeps working.

COCKPIT AND CLOUDS
Cockpit screen protection only supports certain display layouts. It needs frame
generation Off. Use Check screen alignment to inspect it, then turn that tint Off.
Disable protection if the restored area looks misplaced. It is not an all-aircraft
fix. Cloud presets can reduce blockiness by raising input and cloud quality;
they cost graphics time and cannot restore every missing cloud detail.

IF YOU ALSO HAVE INDEPENDENT NATIVE AA
Update both mods. Choose Image processor here or Renderer in Native AA, then
restart X-Plane. Only one mod processes the scene. Each keeps its saved choices.
Native AA is separate and does not require FSR, NR or frame generation.

PLATFORMS AND HELP
This package is Windows x64. Linux has a separate FSR 3.1 preview; it does not
include FSR 4, NR or frame generation. Mac rendering is not available.
Check the live status after loading a flight. If an effect is waiting or off,
read the message rather than assuming a selected option means it is running.
Community: https://discord.gg/yh396QNgHZ
Downloads: https://github.com/cjboy31/xplane-fsr-vulkan/releases
Keep the supplied licenses. No simulator assets or personal settings are bundled.

OPTIONAL SHADOW CONTROLS
The extras/shadows folder contains make XP shadows great again - enhanced
edition.lua and separate instructions. This is an optional FlyWithLua script.
It starts Off; choose 2K, 4K or 8K from FlyWithLua Macros while flying.
4K is the starting point. 8K can use much more graphics memory. It only works
when the simulator offers the needed live control. It is not installed or
enabled automatically, and it does not fix every moving shadow artifact.

STARTUP AND SESSION LOGS
First setup starts with a blank simulator path. Choose your own X-Plane folder.
If the panel recovers your settings but says the renderer did not load, use
Repair startup link when offered, then close X-Plane normally and reopen it.
Missing files still require the full installation package.

Open Diagnostics > Open log folder to get a current copy. After X-Plane exits,
the session log is saved to Output/FSR-Logs/renderer.log inside your simulator
folder. renderer.previous.log keeps the preceding exported session. No log is
uploaded automatically. Share a fresh log when asking for help.

This release reduces repeated buffer setup, including in sessions that started
with FG enabled. It does not guarantee that every aircraft, scenery or graphics
combination is stutter-free. Compare the same view with NR and FG Off first.

UPDATING AN EXISTING INSTALLATION
Setup starts with a recognized existing app folder when available. Keep that
folder to retain settings and local models. If you intentionally choose another
folder, setup can transfer renderer activation only when the previous installed
plugin and its simulator link are verified. It does not overwrite an unknown
or edited plugin. A failed setup now shows its actual reason and the full path
to integration-setup.txt; an ownership error does not mean X-Plane is running.
