# In-simulator controls and third-party launchers

Investigation: 2026-10-30. This is a proposed Windows integration, not a claim
that Visual Engine or launcher-independent injection is already supported.

## What is already a plugin

`Resources/plugins/XPFSR24/64/win.xpl` publishes camera metadata using public
read-only XPLM datarefs and provides an enable/disable menu. The Vulkan layer
does the FSR/NR work. The desktop launcher currently configures that layer
before starting X-Plane. Installing the XPLM plugin alone does not activate
the renderer when another application launches the simulator.

The tested X-Plane 12.4.4-r1 startup log lists Vulkan initialization before
loading XPFSR24 and other XPLM plugins. Required device features must be
requested at `vkCreateDevice`. Enabling the layer in `XPluginStart` is too late
for that session. The local public SDK exposes no native Vulkan device or
command-buffer integration for this purpose. Laminar's
[plugin drawing guidance](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/)
supports OpenGL plugin UI even when the simulator renders using Vulkan/Metal;
that bridge is not access to the simulator's native Vulkan reconstruction pass.

## Recommended design

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
  and device loss. The current NR channel assumes one settings writer; do
  not add a second writer without serializing and merging changes.
- Visual Engine needs its own actual test. Add-on-specific toggles cannot
  establish rendering compatibility. Test cloud/exposure changes, overlays,
  screen restoration and FSR/NR/FG combinations before advertising support.

## Platform limits

Windows Vulkan is the first target. Linux uses loader manifest directories
instead of Windows registry keys and needs native renderer/IPC integration and
testing. X-Plane on macOS renders using Metal; a Vulkan layer or MoltenVK
manifest cannot turn this Windows Vulkan implementation into a Metal plugin.
An in-sim user interface alone does not change these renderer limitations.
