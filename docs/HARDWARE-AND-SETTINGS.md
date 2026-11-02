# Hardware, recommended settings and low FPS

These are starting points for the current FSR mod. They are not a promise of
any FPS number or a reason to buy a particular card. Try the current Release
on your existing system first. Aircraft, weather, airports and other plugins
all affect performance. Updated November 1, 2026.

## Hardware to aim for

| Part | Our suggested starting point | Why it matters |
| --- | --- | --- |
| Windows | Windows 11, 64-bit, with a current stable graphics driver | This is the recommended route for the full Windows feature set. AMD requires Windows 11 for FG 4.0.2. |
| CPU | A recent six-core or better CPU with strong performance per core | X-Plane can run out of CPU time even when the GPU has spare capacity. More cores alone do not guarantee higher FPS. |
| System memory | 32 GB RAM; consider more for unusually heavy scenery and aircraft | This is separate from graphics memory. More RAM does not replace VRAM. |
| Graphics memory | Aim for 12 GB VRAM at 1440p or 16 GB at 4K | These are planning suggestions, not measured minimums. NR, FG, large textures and multiple views need extra room. |
| Storage | SSD for X-Plane and scenery | Helps loading and scenery access; it does not guarantee smooth flying or higher FPS. |

Laminar's own recommended system starts lower, including 16–24 GB RAM and an
8 GB graphics card. The suggestions above leave more room for the mod and
add-ons. Check [X-Plane's current requirements](https://www.x-plane.com/kb/x-plane-12-system-requirements/).

The Radeon AI PRO R9700 is our main Windows test GPU. That is a test reference,
not a requirement or a benchmark for other cards. We do not yet have a verified
FPS chart covering every GPU, aircraft or resolution.

## Which effects fit my GPU?

AMD's SDK hardware support and a successful X-Plane mod test are different.
Use the panel's active status to confirm an effect is actually running.

| Effect | Hardware guidance | What to choose first |
| --- | --- | --- |
| FSR 3.1.6 AA / upscaling | Broad AMD and other-vendor support in AMD's SDK. AMD recommends RX 6000 / RTX 30 series or better for its combined FSR 3.1 upscaling + FG path. This is not a tested mod GPU list. | The first option to compare when FSR 4.1 is unavailable. Intel Arc mod compatibility remains unverified. |
| FSR 4.1.2 AA / upscaling | AMD lists RX 6000, 7000 and 9000 discrete cards and selected Ryzen APUs. That SDK list does not establish flight compatibility for this mod on each device. | Try it on supported Radeon hardware. Use FSR 3.1 if the current runtime reports it unavailable. |
| Frame generation 3.1.6 | AMD's SDK supports RX 5000 and newer or equivalent other-vendor GPUs; older supported hardware may have little performance headroom. | Keep Off until base performance is steady. Aim for about 60 real FPS before enabling it. |
| Frame generation 4.0.2 | AMD specifies RX 9000 series or newer and Windows 11. | Aim for at least 30 steady real FPS; higher is preferable. Being supported does not guarantee smooth results in every view. |
| Neural rendering (NR) | This mod's current Windows NR path needs compatible AMD RDNA4 hardware and a user-imported supported model. Tested on R9700. | Optional. Keep Off for the fastest starting setup. FSR 4.1 support alone does not mean a card supports this NR path. |

The feature versions above match the current SDK 2.4.0 package; the bundle and
effect version numbers differ. AMD's wider FSR 4.1 APU support does not make NR
available on those APUs. Shared-memory graphics also have different memory and
power limits from discrete cards. Start at a lower output resolution with NR Off.

See [AMD's current SDK hardware list](https://gpuopen.com/learn/amd-fsr-sdk-2-4-rx-6000-ryzen-apus/),
[FSR 3.1 hardware recommendations](https://gpuopen.com/learn/amd_fsr_3_1_release/)
and [frame-generation base-FPS guidance](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-swap-chain/).
AMD or driver support does not replace this mod's Windows/Linux platform limits below.

## A good first setup

| Setting | Start here | When to change it |
| --- | --- | --- |
| Image processor | FSR only for this session | If independent Native AA is installed, choose one renderer and restart. |
| Output resolution | Your display's usual resolution | Lower it if the GPU is still too slow after Quality upscaling. Keep your usual menu size. |
| FSR mode / input scale | Upscaling + Quality | Quality draws fewer pixels. Native AA + Full favors native detail and usually costs more GPU time. |
| X-Plane FXAA | Off | Avoid stacking it with the mod's AA. |
| MSAA | Off for the first test | Try 2x afterward if you have spare GPU time. MSAA with FSR remains experimental. |
| Frame generation | Off | Add it after checking real FPS and closing the settings panel. |
| Neural rendering | Off | Add it only if you want its visual changes and have spare GPU time and memory. |
| Clouds and shadows | Moderate settings | Raising cloud clarity or optional shadow resolution can cost FPS and VRAM. Leave the optional shadow script Off for the first test. |

Find these controls inside X-Plane: **Plugins > FSR 3.1 / 4.1 + Frame Gen >
Open settings**. Presets are on **Rendering > Resolution preset**. After choosing
one, open **Image quality**, scroll down and select **Apply graphics after I
close X-Plane**, then quit normally and reopen the sim. The desktop updater
does not contain rendering settings. See the [full walkthrough](INSTALLATION.md).

**Full is correct for Native AA. Quality is correct for upscaling.** Choose the
matching FSR mode and input scale, follow any restart/apply message, then check
the panel after loading a flight. A saved selection does not prove it is active.

Quality uses a 1.5x scale in each direction. These are input/output pairs:

| Output shown on screen | Approximate input with Quality |
| --- | --- |
| 1920 × 1080 | 1280 × 720 |
| 2560 × 1440 | 1707 × 960 |
| 3840 × 2160 | 2560 × 1440 |

Setting **1440p output** does not produce a **4K output** picture. Select 4K
output and Quality input to reconstruct from 1440p. Lower inputs can soften
thin objects, text and clouds. See [AMD's scaling ratios](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/).

## Keep NR affordable

NR changes lighting, color and detail. It adds work rather than speeding up
the simulation. Start with **Auto / Small**, **one pass**, **Balanced 50%** and
**Before upscaling**, with FG Off. Try **Performance 33%** or switch NR Off if
FPS is too low. More passes can blur textures and cost much more GPU time.

Before upscaling saves work when the input is smaller than the output. With
Native AA at Full input, that resolution saving does not exist. Turning down
the effect's visual strength blends in less of the result but still runs the
network; use model size, pass count or Off to reduce its work. Compare the
same stationary view and allow a newly selected model to finish preparing.

Our NR timing checks found a real cost difference between 33%, 50% and 67%.
They do not predict another user's flight FPS. Read the
[measured NR results and their limits](developer/NR-PERFORMANCE-20261031.md) if you want detail.

## Low FPS or stutters: what to check

1. Use the current **Release**. Load the same aircraft, airport, weather and
   camera view each time. Wait for loading and model preparation to finish.
2. Turn NR and FG Off. Compare that view with **Enable FSR processing** On and
   Off, leaving resolution and other settings unchanged. This checks processing
   cost; it is not the same as an uninstalled native-resolution baseline.
3. Show X-Plane's frame-rate data. A higher **CPU** time points toward CPU-side
   work or waits; a higher **GPU** time points toward graphics work. For example,
   0.025 seconds is 25 ms. Allow for driver and synchronization overhead too.
4. If GPU time is high, compare Quality upscaling, lower clouds/shadows or MSAA
   Off. If CPU time stays high, compare fewer world objects, less traffic and
   a simpler aircraft/airport. Test other plugins one at a time. Lowering render
   resolution alone may do little for a CPU limit.
5. If textures turn blurry or stutters grow, check graphics-memory use. Lower
   texture quality/output or switch NR Off, then repeat the same view. Leave
   memory room for scenery; the mod's safeguards cannot control every other
   plugin or prevent all out-of-memory failures.
6. If FG is the only problem, first check base FPS with FG Off. Then test FG
   alone, close the panel and read its live status: FG pauses while setup is
   open. Do not stack it with driver-generated frames or another FG tool.
   There is no universal VSync fix for every display or compatibility method.
7. If it remains slow or says **Bypassed**, share the current session's
   `Output/FSR-Logs/renderer.log` from your X-Plane folder, plus mod/X-Plane/driver
   versions, GPU, output/input resolution, CPU/GPU times and NR/FG settings.
   **Diagnostics > Open log folder** opens a current copy. Logs are not uploaded
   automatically.

Frame generation adds displayed pictures; it does not make aircraft systems or
controls update faster. It can add GPU cost and cannot repair a slow base
simulation. Follow the different base-FPS targets for FG 3.1 and 4.0 above.
For frame-time basics, see [Laminar's performance explanation](https://developer.x-plane.com/2025/12/a-very-quick-performance-primer/).

## Linux and Mac

The full package discussed above is **Windows**. The separate Linux preview
offers only FSR 3.1 upscaling and still needs Linux X-Plane flight testing. It
does not provide FSR 4.1, NR or FG. The Mac package is diagnostic only; there is
no Mac rendering port. See the [current downloads and platform status](../README.md#platforms).
