"""Render an original, captioned feature film using verified captures and local TTS.
SPDX-License-Identifier: MIT
Build-time tools/models are never included in the application package.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--speech-tools", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.speech_tools / "packages"))
    import numpy as np
    import soundfile as sf
    import onnxruntime as ort
    import imageio_ffmpeg
    from kokoro_onnx import Kokoro
    from PIL import Image, ImageDraw, ImageFont

    root = Path(__file__).resolve().parents[1]
    project = json.loads(args.project.read_text(encoding="utf-8-sig"))
    args.output.mkdir(parents=True, exist_ok=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    expected = {"kokoro-v1.0.onnx": "beb0d1848dee9a49da392cc3df26958d46cfa35d321edf434f52949153f0df3a",
                "voices-v1.0.bin": "bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d"}
    for name, digest in expected.items():
        if sha(args.speech_tools / name) != digest:
            raise ValueError("Unreviewed speech model or voice pack: " + name)
    before_status = (args.captures / "external-before-status.txt").read_text().splitlines()
    after_status = (args.captures / "external-after-status.txt").read_text().splitlines()
    if not before_status[0].startswith("Disabled") or after_status[1] != "3.1.6":
        raise ValueError("Comparison state is not established by the capture logs")
    if list(map(int, after_status[2].split()[3:])) != [1920, 1080, 1920, 1080, 1]:
        raise ValueError("Expected a confirmed 1080p Native AA capture")
    captures = {}
    for state in ("before", "after"):
        source = args.captures / ("external-" + state + ".mp4")
        frame = args.output / ("capture-" + state + ".png")
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-ss", "2", "-i", str(source),
                        "-frames:v", "1", "-y", str(frame)], check=True)
        image = Image.open(frame).convert("RGB")
        if image.size != (1920, 1080):
            raise ValueError("Capture dimensions differ")
        captures[state] = image.crop(project["capture"]["crop"])

    opts = ort.SessionOptions()
    opts.intra_op_num_threads = 4
    session = ort.InferenceSession(str(args.speech_tools / "kokoro-v1.0.onnx"), sess_options=opts,
                                  providers=["CPUExecutionProvider"])
    speech = Kokoro.from_session(session, str(args.speech_tools / "voices-v1.0.bin"))
    sample_rate, fps = 24000, 30
    segments, narration, elapsed, srt = [], [], 0.0, []
    def stamp(t):
        ms = round(t * 1000)
        return f"{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}"
    for i, scene in enumerate(project["scenes"]):
        audio, sr = speech.create(scene["narration"], voice=project["voice"], speed=project["speed"], lang="en-gb")
        if sr != sample_rate or not np.isfinite(audio).all() or np.max(np.abs(audio)) <= 0.01:
            raise ValueError("Invalid speech output")
        audio = np.asarray(audio, dtype=np.float32)
        duration = math.ceil((len(audio) / sr + 1.0) * fps) / fps
        lead = int(0.35 * sr)
        padded = np.zeros(round(duration * sr), dtype=np.float32)
        padded[lead:lead+len(audio)] = audio
        narration.append(padded)
        scene.update(start=elapsed, duration=duration)
        segments.append(scene)
        # Open captions are also rendered in the video; this is an editable sidecar.
        srt.append(f"{i+1}\n{stamp(elapsed+0.35)} --> {stamp(elapsed+0.35+len(audio)/sr)}\n{scene['narration']}\n")
        elapsed += duration
        print(f"Narration {i+1}/{len(project['scenes'])}: {duration:.2f}s", flush=True)
    speech_audio = np.concatenate(narration)
    peak = float(np.max(np.abs(speech_audio)))
    speech_audio *= min(1.0, 0.87 / peak)
    sf.write(str(args.output / "narration.wav"), speech_audio, sample_rate, subtype="PCM_16")
    (args.output / "captions.srt").write_text("\n".join(srt), encoding="utf-8")
    (args.output / "transcript.txt").write_text("\n\n".join(s["narration"] for s in segments) + "\n", encoding="utf-8")
    (args.output / "timeline.json").write_text(json.dumps(segments, indent=2), encoding="utf-8")

    W, H = 1920, 1080
    font_root = Path("C:/Windows/Fonts")
    fonts = {size: ImageFont.truetype(str(font_root / "segoeui.ttf"), size) for size in (23, 26, 30, 36, 42, 52, 72, 94)}
    bold = {size: ImageFont.truetype(str(font_root / "seguisb.ttf"), size) for size in (30, 42, 52, 72, 94)}
    ink, muted, gold = "#f1eee5", "#a1adbf", "#dec291"
    bg = np.zeros((H, W, 3), dtype=np.uint8)
    for y in range(H):
        bg[y, :, :] = (12 + y * 6 // H, 16 + y * 7 // H, 23 + y * 9 // H)
    base = Image.fromarray(bg)
    icon = Image.open(root / "assets/app-icon.png").convert("RGBA").resize((150, 150), Image.Resampling.LANCZOS)
    def text(draw, xy, value, size=36, fill=ink, weight=False):
        draw.multiline_text(xy, value, font=(bold if weight else fonts)[size], fill=fill, spacing=15)
    def wrap(draw, value, size, width):
        lines, line = [], ""
        for word in value.split():
            candidate = (line + " " + word).strip()
            if draw.textlength(candidate, font=fonts[size]) > width and line:
                lines.append(line); line = word
            else:
                line = candidate
        if line: lines.append(line)
        return lines
    def caption_pages(draw, scene):
        import re
        chunks = re.split(r"(?<=[.!?])\s+", scene["narration"])
        weights = [max(1, len(c.split())) for c in chunks]
        pages, t = [], 0.35
        available = max(1.0, scene["duration"] - 1.0)
        for chunk, weight in zip(chunks, weights):
            length = available * weight / sum(weights)
            pages.append((t, t + length, "\n".join(wrap(draw, chunk, 30, 1630))))
            t += length
        return pages
    writer = subprocess.Popen([ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "rawvideo",
        "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(fps), "-i", "-", "-i", str(args.output/"narration.wav"),
        "-c:v", "libx264", "-preset", "fast", "-crf", "19", "-profile:v", "high", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "160k", "-af", "loudnorm=I=-18:TP=-2:LRA=7", "-movflags", "+faststart",
        "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709", "-shortest", "-y",
        str(args.output / (project["version"] + ".mp4"))], stdin=subprocess.PIPE)
    total_frames = 0
    try:
        for index, scene in enumerate(segments):
            plate = base.copy()
            d = ImageDraw.Draw(plate)
            text(d, (100, 65), scene["eyebrow"], 26, gold)
            d.line((100, 117, 1820, 117), fill="#313b4b", width=2)
            kind = scene["kind"]
            if kind == "comparison":
                text(d, (100, 147), scene["title"], 52, weight=True)
                for n, state in enumerate(("before", "after")):
                    picture = captures[state].resize((830, 418), Image.Resampling.LANCZOS)
                    x = 100 + n * 890
                    plate.paste(picture, (x, 310))
                    text(d, (x, 254), scene["lines"][n], 30, gold if n else muted, True)
                text(d, (100, 763), "1080p → 1080p  /  MSAA, FXAA and FG Off  /  Matched detail crops", 26, muted)
                text(d, (100, 808), "Actual sequential captures • X-Plane 12.4.4 • Radeon AI PRO R9700", 23, muted)
            elif kind == "welcome":
                plate.paste(icon, (100, 177), icon)
                text(d, (100, 375), scene["title"], 94, weight=True)
                for n, line in enumerate(scene["lines"]): text(d, (104, 670 + n * 60), line, 36, gold if n == 0 else muted)
                d.rounded_rectangle((1230, 208, 1815, 812), 28, fill="#151b25", outline="#313b4b", width=2)
                picture = captures["after"].resize((540, 272), Image.Resampling.LANCZOS)
                plate.paste(picture, (1252, 310))
                text(d, (1260, 635), "CAPTURED IN YOUR SIM", 26, gold)
                text(d, (1260, 685), "Windows Vulkan preview", 30, muted)
            else:
                text(d, (100, 168), scene["title"], 72, weight=True)
                y = 425 if "\n" in scene["title"] else 330
                for n, line in enumerate(scene["lines"]):
                    d.rounded_rectangle((100, y + n * 104, 1820, y + n * 104 + 82), 18, fill="#151b25", outline="#313b4b", width=2)
                    text(d, (132, y + 19 + n * 104), f"{n+1:02}", 30, gold)
                    text(d, (210, y + 17 + n * 104), line, 36, ink)
            pages = caption_pages(d, scene)
            count = round(scene["duration"] * fps)
            for frame in range(count):
                t = frame / fps
                image = plate.copy()
                draw = ImageDraw.Draw(image)
                # Calm, original motion graphics. Captured pixels get the same crop/resize;
                # there is no artificial sharpening, generated detail or altered comparison.
                draw.line((100, 117, 100 + int(1720 * (total_frames / max(1, round(elapsed*fps)))), 117), fill=gold, width=3)
                draw.rounded_rectangle((86, 930, 1834, 1040), 16, fill="#080b10")
                page = next((p[2] for p in pages if p[0] <= t < p[1]), pages[-1][2] if t >= pages[-1][1] else "")
                for row, line in enumerate(page.splitlines()[:2]):
                    width = draw.textlength(line, font=fonts[30])
                    text(draw, ((W-width)/2, 946 + 41*row), line, 30)
                alpha = min(1.0, t / 0.35, max(0.0, (scene["duration"] - t) / 0.3))
                if alpha < 1: image = Image.blend(base, image, alpha)
                writer.stdin.write(image.tobytes())
                total_frames += 1
            plate.save(args.output / f"scene-{index+1:02}.jpg", quality=90)
            print(f"Rendered scene {index+1}/{len(segments)}", flush=True)
    finally:
        writer.stdin.close()
    if writer.wait() != 0:
        raise RuntimeError("Video encoding failed")
    movie = args.output / (project["version"] + ".mp4")
    (args.output / "feature-video.ini").write_text(f"[Video]\nVersion={project['version']}\nBytes={movie.stat().st_size}\nSHA256={sha(movie)}\n", encoding="ascii")
    provenance = {"project": project, "seconds": elapsed, "frames": total_frames, "fps": fps,
                  "movie_sha256": sha(movie), "voice": project["voice"], "speech_models": expected,
                  "capture_sha256": {p.name: sha(p) for p in args.captures.glob("external-*.mp4")},
                  "comparison_policy": "Matched crop and resize only. No sharpening, generated detail, fabricated FPS or claimed universal ghosting fix."}
    (args.output / "provenance.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    print("BUILT", movie, elapsed, "seconds", movie.stat().st_size, "bytes", flush=True)


if __name__ == "__main__":
    main()
