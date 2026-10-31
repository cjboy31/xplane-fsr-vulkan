make XP shadows great again - enhanced edition

Optional shadow-size controls for FlyWithLua NG on X-Plane 12.
This is separate from AA. Install it only if you want to try it.

INSTALL
1. Install the X-Plane 12 version of FlyWithLua NG from its official project.
2. Close X-Plane. Copy the .lua file beside this text into:
   Resources/plugins/FlyWithLua/Scripts
3. Start X-Plane normally. Open Plugins > FlyWithLua > Macros.
4. Select Make XP shadows: 4K to begin, or Off / restore to undo it.

The script starts Off every session. Choose a size while flying. 2K costs less;
4K is a starting point; 8K can use four times the shadow-map memory of 4K.
This can sharpen shadow edges but does not fix every moving or flickering shadow.
Keep X-Plane's shadow quality enabled. Do not use another shadow-size script at
the same time. A different add-on's changed value is preserved when turning Off.

The script checks that the simulator provides a writable shadow-size control.
If it is missing, no setting is changed. This is an experimental option because
Laminar can change this internal control in later X-Plane versions. A successful
value change alone does not prove that every shadow map was rebuilt immediately.
Check the actual view before deciding that a larger size helps.

Remove the Lua file with X-Plane closed to uninstall. It never edits settings.txt,
controller settings, weather or either mod's AA settings. No FlyWithLua binary
is included. Windows/Linux/Mac use their own FlyWithLua build; actual simulator
tests are listed in docs/SHADOW-RESEARCH.md, not assumed from the script's code.
