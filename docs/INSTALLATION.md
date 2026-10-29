# Install and inspect the SR22 Vulkan cockpit preview

Use the complete release package or build `app/` as described in BUILDING.md.
Close X-Plane and the previous launcher before installing/updating anything.

1. Run the launcher and select the folder containing `X-Plane.exe`.
2. Run **Native AA check**, then **Run cockpit check**. View the analytical
   cockpit comparison; it is a GPU regression fixture, not live-simulator proof.
3. Open **Graphics setup** and choose a preset. `4K - Quality` configures
   2560x1440 internal rendering with 3840x2160 output. Presets apply immediately
   and back up changed preferences. MSAA/FXAA and FG are set Off.
4. Keep **Restore SR22 cockpit screens** enabled and **frame generation Off**.
5. Enable **Show green screen-alignment tint**, then launch the selected mode
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

Graphics changes require X-Plane closed. Exact files/change manifests are kept
in `graphics-backups/`. **Restore last change** restores only matching changed
keys; it refuses to overwrite later conflicting choices. Before uninstalling,
restore graphics if wanted, close X-Plane and use **Remove camera plugin**.
Then close the launcher and run the package uninstaller. Backups/logs are retained.

The existing FG path is available with cockpit restore Off. It remains experimental
and includes UI in interpolation. VR, multiple windows, multi-GPU and complete
object/cloud motion are unsupported. Read STATUS.md before contributing results.
