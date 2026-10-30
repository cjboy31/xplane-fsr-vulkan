"""Convert a lossless RGBA FP16 desktop capture into a documented SDR preview.

SPDX-License-Identifier: MIT
This is a fixed color-management conversion, not retouching or an artifact fix.
Bypass FFmpeg's packed-half to planar-float scaler: the tested build clipped >1
before conversion. Keep the original half-float bytes as the capture master.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys


def convert(rgb, white_nits, peak_nits, np):
    if not all(map(math.isfinite, (white_nits, peak_nits))) or white_nits <= 0 or peak_nits < white_nits:
        raise ValueError("Expected measured display white and peak luminance")
    if not np.isfinite(rgb).all():
        raise ValueError("Capture contains non-finite pixels")
    # scRGB 1.0 = 80 nit. Normalize by the same measured white level for A/B.
    x = np.maximum(rgb, 0) / (white_nits / 80)
    luminance = x @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    white = peak_nits / white_nits
    # Fixed, luminance-based extended Reinhard curve, with display-peak white.
    target = np.clip(luminance * (1 + luminance / (white * white)) / (1 + luminance), 0, 1)
    mapped = x * (target / np.maximum(luminance, 1e-8))[..., None]
    chroma = mapped - target[..., None]
    high, low = chroma.max(axis=-1), chroma.min(axis=-1)
    saturation = np.minimum(1, np.minimum((1-target)/np.maximum(high, 1e-8),
                                          target/np.maximum(-low, 1e-8)))
    # Fit out-of-gamut channels around their luminance, rather than channel clipping.
    linear = np.clip(target[..., None] + chroma * saturation[..., None], 0, 1)
    return np.where(linear <= 0.0031308, linear * 12.92,
                    1.055 * np.power(linear, 1/2.4) - 0.055)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--packages", type=Path, required=True)
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--frame", type=int, default=-1)
    p.add_argument("--white-nits", type=float, required=True)
    p.add_argument("--peak-nits", type=float, required=True)
    args = p.parse_args()
    if args.width <= 0 or args.height <= 0:
        p.error("Capture width and height must be positive")
    sys.path.insert(0, str(args.packages))
    import numpy as np
    from PIL import Image, PngImagePlugin
    frame_bytes = args.width * args.height * 8
    count, remainder = divmod(args.source.stat().st_size, frame_bytes)
    if remainder or not count or args.frame < -count or args.frame >= count:
        raise ValueError("Invalid FP16 capture dimensions/frame count")
    if args.output.exists():
        raise ValueError("Keep earlier captures intact; choose a fresh output")
    index = args.frame % count
    with args.source.open("rb") as f:
        f.seek(index * frame_bytes)
        rgb = np.frombuffer(f.read(frame_bytes), dtype="<f2").reshape(args.height, args.width, 4)[..., :3].astype(np.float32)
    result = convert(rgb, args.white_nits, args.peak_nits, np)
    info = PngImagePlugin.PngInfo()
    info.add(b"sRGB", b"\x00")
    Image.fromarray(np.uint8(np.rint(result*255))).save(args.output, pnginfo=info)
    # Hash streaming so multi-frame masters do not need a second memory copy.
    h = hashlib.sha256()
    with args.source.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            h.update(block)
    report = {"source_sha256": h.hexdigest(), "frames": count, "selected_frame": index,
              "source_format": "RGBA FP16 little-endian; linear scRGB; 1.0 = 80 nit",
              "width": args.width, "height": args.height,
              "white_nits": args.white_nits, "peak_nits": args.peak_nits,
              "rgb_min": float(rgb.min()), "rgb_max": float(rgb.max()),
              "source_channels_over_one_fraction": float(np.mean(rgb > 1)),
              "output_all_white_fraction": float(np.mean(np.min(result, axis=-1) > 0.995)),
              "transform": "Fixed measured-white normalization, extended Reinhard luminance mapping, gamut fit, sRGB encoding",
              "selective_edits": False, "renderer_artifact_repair": False,
              "output_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest()}
    args.output.with_suffix(".color.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
