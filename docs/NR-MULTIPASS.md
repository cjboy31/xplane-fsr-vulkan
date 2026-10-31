# Repeated NR passes and texture detail

Development investigation, 2026-10-30. The owner reported ground textures becoming
blurry at three passes. One pass remains the recommended flight setting.

The pinned upstream runtime feeds each pass's edited model image to the next
pass. Its final transfer runs once against the untouched scene input. This is
repeated neural processing, rather than three AA samples or higher source
resolution. Repeated edits can suppress texture contrast and alter lighting.

The previous GPU check allocated a three-pass context, but its frame sequence
actually requested only one and two passes. The normal stress sequence now
includes three. `NRCheck --pass-study MODEL_PERCENT PASSES` also runs 32 static,
matched frames with an explicit 1/2/3 count and writes float input/output plus
metadata. `--pass-study-rgba32f` checks the other supported HDR format.

## Conservative composition correction

The adapter divides the final Detail transfer strength by the **actual** pass
count, bounded by the allocated context capacity. The network still executes
the selected number of passes, but its accumulated edit has a shared composition
budget against the original full-resolution input: for example, 50% Detail and
three passes merge the accumulated result at approximately 17%. This deliberately
reduces the final model influence. Saved Detail/Passes values,
model scale, FSR, cloud settings and resolution are retained. The current pass
count and protected detail budget are reported in NR status. The controls and
context preparation remain live.

This retains the previous one-pass expression. It adds no sharpening, texture
LOD override, ground mask or extra image-resizing stage. Three passes still
have roughly three network evaluations' cost and can change the scene's look.
Start with one pass and reduce Detail transfer if textures soften. Compare model
resolution in the same view: higher model resolution lets the network edit more
source detail and does not guarantee better retention of the original texture.

## Measured development results and limits

On the owner's R9700, a 512 x 288 linear RGBA16F synthetic texture fixture used
the small model at 67%, 100% model intensity, Cinematic conditioning and 50%
Detail transfer. All compared cases had byte-identical input. A brightness-
normalized, four-pixel texture-detail measurement had coherent gains of 0.931
with one pass and 0.847 with the previous three-pass composition. The protected
three-pass implementation measured 0.949. This measures retained input texture in
this fixture; it is not a percentage improvement in X-Plane visual quality.

The integrated one-pass output was byte-identical to the previous backend.
Three-pass GPU medians were approximately 4.13 ms protected and 4.07 ms previous
in this small fixture. No performance improvement is claimed.

Upstream's optional detail-only cascade was also evaluated. It reduced broad
tone drift but did not sufficiently retain the fine texture signal, so it was
not incorporated. The pinned upstream source and shaders remain unchanged.

The integrated build passed actual one/two/three-pass RGBA16F, three-pass RGBA32F,
live-bypass/history-reset stress and one-pass-capacity checks, with finite HDR outputs. Its one-pass result was
byte-identical to the previous backend in both context capacities. A matched
live ground-scene comparison is still required before calling this a confirmed
simulator blur fix. Model output can still soften or invent details. The synthetic fixture does not establish
aircraft, cockpit-text, moving-cloud or all-weather quality. Higher model
resolution also suppressed some fine synthetic texture; it is not advertised
as an automatic ground-clarity fix.

The first cold shader preparation in the study took about 43 seconds, primarily
pipeline compilation. Subsequent matching graph builds were under a second.
These are preparation measurements, not a diagnosis of simulator CPU frame time.
