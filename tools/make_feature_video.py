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
import re
from capture_evidence import public_provenance, verify_hdr_comparison


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--capture-config", type=Path,
                        help="Private, local capture configuration for background-only guides; never published")
    parser.add_argument("--speech-tools", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse-narration", type=Path,
                        help="Reuse verified narration/timing from an earlier render with the same script and voice")
    parser.add_argument("--preview", action="store_true", help="Write an unpublished review MP4, without an installer manifest")
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
    if project["capture"].get("background_only"):
        if not args.capture_config:
            raise ValueError("Background-only guides require a private --capture-config")
        capture = json.loads(args.capture_config.read_text(encoding="utf-8-sig"))
        project["capture"] = {**capture, **project["capture"]}
    args.output.mkdir(parents=True, exist_ok=False)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    preview_id = project.get("preview_id", "v2")
    if not re.fullmatch(r"v[0-9]+", preview_id):
        raise ValueError("Invalid preview edition")
    movie = args.output / (f"FSR-Aviation-Guide-Preview-{preview_id}.mp4" if args.preview else project["version"] + ".mp4")
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    expected = {"kokoro-v1.0.onnx": "beb0d1848dee9a49da392cc3df26958d46cfa35d321edf434f52949153f0df3a",
                "voices-v1.0.bin": "bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d"}
    for name, digest in expected.items():
        if sha(args.speech_tools / name) != digest:
            raise ValueError("Unreviewed speech model or voice pack: " + name)
    hdr_evidence = None
    hdr_images = None
    if "evidence" in project["capture"]:
        hdr_images, hdr_evidence = verify_hdr_comparison(args.captures, project["capture"]["evidence"])
    else:
        before_status = (args.captures / "external-before-status.txt").read_text().splitlines()
        after_status = (args.captures / "external-after-status.txt").read_text().splitlines()
        if not before_status[0].startswith("Disabled") or after_status[1] != "3.1.6":
            raise ValueError("Comparison state is not established by the capture logs")
        if list(map(int, after_status[2].split()[3:])) != [1920, 1080, 1920, 1080, 1]:
            raise ValueError("Expected a confirmed 1080p Native AA capture")
    captures = {}
    for state in ("before", "after"):
        frame = args.output / ("capture-" + state + ".png")
        if hdr_images:
            frame.write_bytes(hdr_images[state].read_bytes())
        else:
            source = args.captures / ("external-" + state + ".mp4")
            subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-ss", "2", "-i", str(source),
                            "-frames:v", "1", "-y", str(frame)], check=True)
        image = Image.open(frame).convert("RGB")
        if image.size != (1920, 1080):
            raise ValueError("Capture dimensions differ")
        captures[state] = image.crop(project["capture"]["crop"])

    sample_rate, fps = 24000, 30
    cached = None
    if args.reuse_narration:
        cached = json.loads((args.reuse_narration / "provenance.json").read_text(encoding="utf-8"))
        if (cached["voice"] != project["voice"] or cached["project"]["speed"] != project["speed"] or
                cached["speech_models"] != expected or
                [s["narration"] for s in cached["project"]["scenes"]] !=
                [s["narration"] for s in project["scenes"]]):
            raise ValueError("Cached narration does not match this script, voice, speed and model provenance")
        for name, digest in cached["capture_sha256"].items():
            if sha(args.captures / name) != digest:
                raise ValueError("Comparison capture changed since the earlier render")
        if cached.get("narration_sha256") and sha(args.reuse_narration / "narration.wav") != cached["narration_sha256"]:
            raise ValueError("Cached narration audio changed since the earlier render")
        speech_audio, sr = sf.read(args.reuse_narration / "narration.wav", dtype="float32")
        if sr != sample_rate or speech_audio.ndim != 1 or not np.isfinite(speech_audio).all():
            raise ValueError("Invalid cached narration")
        for scene, previous in zip(project["scenes"], cached["project"]["scenes"]):
            scene.update(start=previous["start"], duration=previous["duration"])
    else:
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 4
        session = ort.InferenceSession(str(args.speech_tools / "kokoro-v1.0.onnx"), sess_options=opts,
                                      providers=["CPUExecutionProvider"])
        speech = Kokoro.from_session(session, str(args.speech_tools / "voices-v1.0.bin"))
    segments, narration, elapsed, srt = [], [], 0.0, []
    def stamp(t):
        ms = round(t * 1000)
        return f"{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}"
    for i, scene in enumerate(project["scenes"]):
        if cached:
            scene.update(start=elapsed, duration=cached["project"]["scenes"][i]["duration"])
            segments.append(scene)
            elapsed += scene["duration"]
            continue
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
        elapsed += duration
        print(f"Narration {i+1}/{len(project['scenes'])}: {duration:.2f}s", flush=True)
    if not cached:
        speech_audio = np.concatenate(narration)
    if len(speech_audio) != round(elapsed * sample_rate):
        raise ValueError("Narration duration does not match the scene timeline")
    peak = float(np.max(np.abs(speech_audio)))
    speech_audio *= min(1.0, 0.87 / peak)
    sf.write(str(args.output / "narration.wav"), speech_audio, sample_rate, subtype="PCM_16")
    (args.output / "transcript.txt").write_text("\n\n".join(s["narration"] for s in segments) + "\n", encoding="utf-8")
    (args.output / "timeline.json").write_text(json.dumps(segments, indent=2), encoding="utf-8")

    W, H = 1920, 1080
    font_root = Path("C:/Windows/Fonts")
    fonts = {size: ImageFont.truetype(str(font_root / "segoeui.ttf"), size) for size in (18, 20, 23, 26, 30, 36, 42, 52, 60, 72, 88, 94)}
    bold = {size: ImageFont.truetype(str(font_root / "seguisb.ttf"), size) for size in (30, 42, 52, 60, 72, 88, 94)}
    ink, muted, gold = "#f1eee5", "#a1adbf", "#dec291"
    bg = np.zeros((H, W, 3), dtype=np.uint8)
    for y in range(H):
        bg[y, :, :] = (12 + y * 6 // H, 16 + y * 7 // H, 23 + y * 9 // H)
    base = Image.fromarray(bg)
    icon = Image.open(root / "assets/app-icon.png").convert("RGBA").resize((100, 100), Image.Resampling.LANCZOS)
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
        chunks = re.split(r"(?<=[.!?])\s+", scene["narration"])
        weights = [max(1, len(c.split())) for c in chunks]
        pages, t = [], 0.35
        available = max(1.0, scene["duration"] - 1.0)
        for chunk, weight in zip(chunks, weights):
            length = available * weight / sum(weights)
            readable = chunk
            for spoken, written in [("M S A A", "MSAA"), ("F X A A", "FXAA"), ("F S R", "FSR"),
                                    ("Native A A", "Native AA"), ("ten eighty p", "1080p"),
                                    ("fourteen forty p", "1440p"), ("four K", "4K"),
                                    ("X Plane", "X-Plane"), ("anti aliasing", "anti-aliasing")]:
                readable = readable.replace(spoken, written)
            readable = readable.replace("H D R", "HDR").replace("S D R", "SDR")
            pages.append((t, t + length, "\n".join(wrap(draw, readable, 30, 1630))))
            t += length
        return pages

    def aircraft_outline(draw, cx, cy, scale=1.0, color="#334253"):
        # Original small plan-view aircraft mark, rather than a cockpit HUD.
        points = [(0,-30),(4,-10),(29,7),(29,13),(5,4),(5,23),(13,30),
                  (13,34),(0,28),(-13,34),(-13,30),(-5,23),(-5,4),
                  (-29,13),(-29,7),(-4,-10),(0,-30)]
        draw.line([(round(cx+x*scale),round(cy+y*scale)) for x,y in points], fill=color, width=2)

    plates, all_pages = [], []
    chapter_defaults = ["WELCOME", "COMPARE", "PRESETS", "RESOLUTION", "LIVE STATUS", "LIMITS", "WHAT'S NEW"]
    chapters = [scene.get("chapter", chapter_defaults[index])
                for index, scene in enumerate(segments)]
    for index, scene in enumerate(segments):
        plate = base.copy()
        d = ImageDraw.Draw(plate)
        text(d, (100, 59), scene["eyebrow"], 26, gold)
        text(d, (1460, 65), "FLIGHT BRIEF", 20, muted)
        aircraft_outline(d, 1786, 74, 0.6, gold)
        d.line((100, 117, 1820, 117), fill="#273341", width=2)
        kind = scene["kind"]
        if kind == "comparison":
            text(d, (100, 156), scene["title"], 60, weight=True)
            for n, state in enumerate(("before", "after")):
                picture = captures[state].resize((830, 418), Image.Resampling.LANCZOS)
                x = 100 + n * 890
                plate.paste(picture, (x, 323))
                text(d, (x, 266), scene["lines"][n], 30, gold if n else muted, True)
            text(d, (100, 770), project["capture"].get("caption", "1080p → 1080p  /  MSAA, FXAA and FG Off  /  Matched detail crops"), 26, muted)
            if project["capture"].get("warning"):
                text(d, (100, 812), project["capture"]["warning"], 26, gold)
            else:
                text(d, (100, 812), "Actual sequential captures • X-Plane 12.4.4 • Radeon AI PRO R9700", 23, muted)
        elif kind == "welcome":
            plate.paste(icon, (100, 179), icon)
            text(d, (100, 334), scene["title"], 88, weight=True)
            for n, line in enumerate(scene["lines"]):
                text(d, (104, 622+n*58), line, 36, gold if n == 0 else muted)
            # Faint horizon arcs and a restrained photo panel are the only
            # larger aviation accents; the actual comparison stays unchanged.
            d.arc((1180, 159, 1850, 829), 205, 335, fill="#24313f", width=2)
            d.rounded_rectangle((1230, 230, 1815, 790), 26, fill="#151b25", outline="#313b4b", width=2)
            diagnostic = bool(project["capture"].get("warning"))
            picture = captures["before" if diagnostic else "after"].resize((540, 272), Image.Resampling.LANCZOS)
            plate.paste(picture, (1252, 302))
            text(d, (1260, 626), "HDR-CORRECTED CAPTURE" if diagnostic else "CAPTURED IN YOUR SIM", 26, gold)
            text(d, (1260, 673), "Renderer quality under review" if diagnostic else "Windows Vulkan preview", 30, muted)
        else:
            text(d, (100, 168), scene["title"], 72, weight=True)
            y = 425 if "\n" in scene["title"] else 330
            for n, line in enumerate(scene["lines"]):
                row = y+n*104
                d.rounded_rectangle((100,row,1820,row+82), 16, fill="#151b25", outline="#2b3745", width=2)
                text(d, (132,row+19), f"{n+1:02}", 30, gold)
                text(d, (210,row+17), line, 36, ink)
        pages = caption_pages(d, scene)
        plates.append(plate)
        all_pages.append(pages)
        plate.save(args.output / f"scene-{index+1:02}.jpg", quality=92)
        for start, end, caption in pages:
            srt.append(f"{len(srt)+1}\n{stamp(scene['start']+start)} --> {stamp(scene['start']+end)}\n{caption}\n")
    (args.output / "captions.srt").write_text("\n".join(srt), encoding="utf-8")

    def ease(t):
        t = max(0.0, min(1.0, t))
        return t*t*(3.0-2.0*t)

    transition = 0.6
    half_transition = transition/2
    writer = subprocess.Popen([ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "rawvideo",
        "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(fps), "-i", "-", "-i", str(args.output/"narration.wav"),
        "-c:v", "libx264", "-preset", "fast", "-crf", "19", "-profile:v", "high", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "160k", "-af", "loudnorm=I=-18:TP=-2:LRA=7", "-ar", "48000", "-movflags", "+faststart",
        "-vf", "scale=in_range=full:out_range=tv:out_color_matrix=bt709,setparams=range=limited:color_primaries=bt709:color_trc=iec61966-2-1:colorspace=bt709",
        "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-colorspace", "bt709", "-color_range", "tv", "-shortest", "-y",
        str(movie)], stdin=subprocess.PIPE)
    total_frames = 0
    try:
        for index, scene in enumerate(segments):
            plate, pages = plates[index], all_pages[index]
            count = round(scene["duration"] * fps)
            for frame in range(count):
                t = frame / fps
                image = plate.copy()
                # A continuous eased dissolve crosses each scene boundary.
                # No internal fade-to-black, zoom, wipe, flash or whoosh.
                if index > 0 and t < half_transition:
                    image = Image.blend(plates[index-1], plate, ease(0.5+t/transition))
                elif index+1 < len(plates) and t > scene["duration"]-half_transition:
                    alpha = ease((t-(scene["duration"]-half_transition))/transition)
                    image = Image.blend(plate, plates[index+1], alpha)
                draw = ImageDraw.Draw(image)
                # A quiet flight-plan line connects the chapters; it stays in
                # place through transitions and never covers the comparison.
                now = total_frames/fps
                draw.line((100,876,1820,876), fill="#303e4d", width=2)
                xprogress = 100+round(1720*min(1.0,now/elapsed))
                draw.line((100,876,xprogress,876), fill=gold, width=2)
                for n, chapter in enumerate(segments):
                    x = 100+round(1720*chapter["start"]/elapsed)
                    done = now >= chapter["start"]
                    draw.ellipse((x-5,871,x+5,881), fill=gold if done else "#111923", outline=gold if done else "#627186", width=2)
                    text(draw, (x,894), chapters[n], 18, gold if index == n else muted)
                draw.rounded_rectangle((86,945,1834,1044), 14, fill="#080b10")
                page = next((p[2] for p in pages if p[0] <= t < p[1]), pages[-1][2] if t >= pages[-1][1] else "")
                if t >= pages[-1][1]:
                    page = ""
                for row, line in enumerate(page.splitlines()[:2]):
                    width = draw.textlength(line, font=fonts[30])
                    text(draw, ((W-width)/2, 957 + 39*row), line, 30)
                if args.preview:
                    text(draw, (1510, 1060), "PRIVATE PREVIEW · " + preview_id.upper(), 18, muted)
                if index == 0 and t < 0.6:
                    image = Image.blend(base,image,ease(t/0.6))
                if index == len(segments)-1 and scene["duration"]-t < 0.8:
                    image = Image.blend(base,image,ease((scene["duration"]-t)/0.8))
                writer.stdin.write(image.tobytes())
                total_frames += 1
            print(f"Rendered scene {index+1}/{len(segments)}", flush=True)
    finally:
        writer.stdin.close()
    if writer.wait() != 0:
        raise RuntimeError("Video encoding failed")
    if not args.preview:
        (args.output / "feature-video.ini").write_text(f"[Video]\nVersion={project['version']}\nBytes={movie.stat().st_size}\nSHA256={sha(movie)}\n", encoding="ascii")
    provenance = {"project": project, "seconds": elapsed, "frames": total_frames, "fps": fps,
                  "movie_sha256": sha(movie), "voice": project["voice"], "speech_models": expected,
                  "capture_sha256": {p.name: sha(p) for p in args.captures.glob("external-*.mp4")},
                  "hdr_capture_evidence": hdr_evidence,
                  "narration_sha256": sha(args.output / "narration.wav"),
                  "publication_state": "Private review; owner approval required before publication" if args.preview else "Rendered; follow release workflow",
                  "transition": {"style":"eased cross-dissolve; shared background; no internal black frames", "seconds":transition},
                  "reused_narration_sha256":sha(args.reuse_narration/"narration.wav") if cached else None,
                  "comparison_policy": "Matched crop and resize only. No sharpening, generated detail, fabricated FPS or claimed universal ghosting fix."}
    if not args.preview:
        provenance = public_provenance(provenance)
    (args.output / "provenance.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    print("BUILT", movie, elapsed, "seconds", movie.stat().st_size, "bytes", flush=True)


if __name__ == "__main__":
    main()
