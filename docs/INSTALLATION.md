FSR 3/4 + FRAME GEN FOR X-PLANE 12 — 0.7.5-alpha

INSTALL OR UPDATE
1. Close X-Plane normally. Close any plugin updater or test windows.
2. Extract the RAR and run the setup inside it, or run the standalone Setup.exe.
3. Select the X-Plane folder containing X-Plane.exe. External drives are fine.
4. Enable in-sim integration. Keep the default application folder, or choose
   your existing official installation when updating. Saved choices are kept.
5. Launch X-Plane with your normal shortcut or usual third-party launcher.
6. Open Plugins > FSR 3/4 + Frame Gen > Open settings.

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
  need a new session. For graphics edits, choose Apply after I close X-Plane,
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
