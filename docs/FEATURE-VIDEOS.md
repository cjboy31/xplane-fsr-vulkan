# Versioned feature videos

Windows 0.6.2 adds an offline feature guide after initial launch/update. The MP4
is bundled with the installer, not streamed, and the launcher checks its version,
size and SHA-256 before opening it. No web view, external script, speech model,
media tool, telemetry or simulator rendering hook is added.

The guide appears once for each installed app version. Skip, Escape, Close and
finishing playback all allow immediate continuation; presentation counts even
when skipped. **What's new** replays it. The playback window has Pause/Play,
Mute/Unmute and **Show a feature video after future app updates**. Audio mute,
automatic-video preference and last-presented version are saved in the existing
INI's independent `FeatureVideo` section, preserved by ordinary preference saves
and recognized installer updates. Missing/bad media skips automatic presentation;
replay explains the missing package. Decoder failure leaves Skip available and
does not prevent using the application. Media Foundation is loaded only for
playback so missing optional media components do not prevent launcher startup.
The player cannot open while a simulator launched by that window is running.

This onboarding implementation is for the regular Windows launcher. Separate
Linux/Mac preview packages are unchanged. The video explains their distinct
limits and does not advertise the Mac diagnostic probe as an FSR renderer.

## Editorial policy

The owner reviews video revisions before publication. Deliver a local preview
first and wait for explicit approval before uploading or replacing release
media. Keep already published artifacts intact during review. Use `--preview`
for a clearly named review MP4 without an installation manifest. The current
aviation review project is `media/source/0.6.3-alpha.json` (preview v6).

The v6 revision uses fresh FP16 captures with the corrected camera publisher and
a documented, identical HDR-to-SDR conversion on both sides. The comparison is
a stationary hold from sequential, acknowledged camera sweeps; it does not
establish performance or absence of all motion artifacts. Keep earlier reviews
and the published release intact. See [artifact investigation](ARTIFACT-INVESTIGATION.md).
Local display calibration and raw-capture evidence stay private; release
provenance includes only the film's credits, integrity and comparison method.
The film focuses
on features, changes, setup and intended benefits. Keep the technical defect
diagnosis in developer documentation, outside narration and on-screen copy.

Keep the tone calm and the claims specific. Prefer 45–100 seconds, readable
captions, restrained motion and natural preset-voice narration. Avoid unsupported
FPS numbers and claims that ghosting has been eliminated. Show disabled or
experimental features accurately. Every update's film should explain its actual
changes and intended effect; do not reuse a general overview as a claim that new
rendering improvements were delivered.

Before/after samples must come from the real mod/simulator with recorded state.
Keep camera, resolution and comparison settings matched, disclose which AA/input
settings differ and whether captures are sequential. Apply identical crop/resize;
do not sharpen, blur, add generated detail or synthesize a better after image.
Retain private evidence locally. Only publish reviewed footage, narration,
captions and non-sensitive provenance; never publish simulator shader/assets,
credentials or user logs/preferences. A visually pleasing frame does not prove
correct motion in clouds, propellers or all aircraft.

## Build the film

`media/source/0.6.2-alpha.json` is the editable scene/narration project. The
initial overview includes a fresh 1080p default-Cirrus external-view comparison:
processing bypassed versus FSR 3.1.6 Native AA, MSAA/FXAA/FG off. The renderer
used for capture was the existing 0.6.1 build; 0.6.2 changes onboarding, not its
reconstruction algorithm. It is an individual example, not an FPS benchmark or
proof of a general cloud/ground/cockpit ghosting fix.

For an HDR desktop, do not use an 8-bit desktop-duplication recording as the
comparison master. The tested FFmpeg build also clipped values above 1 when
converting packed half-float RGB to planar float. Capture packed `rgbaf16le`
without that intermediate conversion and retain it locally. Convert using
`tools/convert_scrgb_capture.py` with measured SDR-white/display-peak luminance;
apply exactly the same transform to both states. Retain the source hash, color
report, processing-state evidence and PNG hash in a comparison manifest. The
video builder validates those files for projects with `capture.evidence`.
Tone mapping cannot repair renderer artifacts or recover already clipped source
pixels. Never retouch the after image to imply a rendering improvement.

`tools/make_feature_video.py` checks capture-state evidence and pinned build-time
speech assets, generates original preset-voice narration locally, renders the
original charcoal/champagne artwork, and encodes H.264/AAC MP4 plus open captions,
editable captions/transcript, timeline and provenance. It does not upload inputs
to a speech service. The film uses Kokoro's `bm_george` preset, not a cloned or
named person's voice. The weights are Apache-2.0; build-time tools/models stay
outside the app/source distribution. Preserve credits in the installer.

The revised edit uses a 0.6-second eased cross-dissolve across each chapter
boundary, a shared navy/champagne background and restrained aircraft/flight-plan
details. Internal transitions never fade to black. Captures remain stationary,
with identical crop/resize. `--reuse-narration <previous-render-folder>` verifies
the script, preset voice, speed, capture hashes and model provenance before
reusing the original narration and timing. A new script requires a fresh voice
render. Open captions display readable acronyms rather than TTS spellings.

```powershell
python tools/make_feature_video.py --project media/source/0.6.2-alpha.json `
  --captures <reviewed-capture-folder> --speech-tools <local-build-tools> `
  --output <fresh-render-folder>
```

Build dependencies: Pillow, NumPy, kokoro-onnx 0.6.1, ONNX Runtime CPU, soundfile,
imageio-ffmpeg and their documented dependencies. The current renderer uses
Windows Segoe UI fonts to rasterize titles; font files are not redistributed.
Review all scenes and listen to the final narration, check readable captions,
duration, full decode, audio peak/loudness and actual in-app playback. Verify
first launch, Skip, replay, same-version reopen, next-version and opt-out state,
failed/missing media and settings preservation. Recheck playback after packaging.

Stage only the versioned MP4, feature-video.ini, TRANSCRIPT.txt, CAPTIONS.srt,
PROVENANCE.json and CREDITS-AND-LICENSES.txt for Build-Installer. The installer
validates exact video bytes; no model/runtime tools or raw captures are staged.
Publish the exact tested MP4 as a separate release asset, alongside its hash,
for users who want to watch before installing. Native preview release policy is
separate; do not mislabel this guide as a native Mac rendering feature.

Sources: [Microsoft Media Engine](https://learn.microsoft.com/en-us/windows/win32/api/mfmediaengine/nn-mfmediaengine-imfmediaengine),
[Kokoro model card/license](https://huggingface.co/hexgrad/Kokoro-82M),
[kokoro-onnx source](https://github.com/thewh1teagle/kokoro-onnx).
