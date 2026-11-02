"""Measure how well a motion field explains the next frame (original code).

SPDX-License-Identifier: MIT
Input: a folder written by the layer's motion-quality capture
(XPFSR24_MOTION_CAPTURE, see docs/developer/MOTION-VECTORS-PLAN.md): capture.json
plus frame0.bin / frame1.bin holding colour, depth, motion and reactive data
exactly as FSR received them (shared motion contract v1).

For each sampled pixel of frame 1 the tool looks up where the motion says the
same content was in frame 0, samples frame 0 there (bilinear) and compares
brightness. The same comparison with zero motion is the baseline. "Gain" is
how much of the baseline difference the vectors remove: 100% is perfect, 0%
is no better than not moving the history, negative is worse than nothing.
Results are split by region: sky, own aircraft / cabin (pixels the producer
marked with a partial reactive value), near (< 100 m) and far scenery.

Standard library only, so it runs on any tester PC. Example:
    python tools/motion_quality.py <capture-folder> [--step 2] [--json out.json]
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

FORMAT_CHANNELS = {
    97: ("half", 4),     # R16G16B16A16_SFLOAT
    109: ("float", 4),   # R32G32B32A32_SFLOAT
    37: ("rgba8", 4), 43: ("rgba8", 4),   # R8G8B8A8 UNORM / SRGB
    44: ("bgra8", 4), 50: ("bgra8", 4),   # B8G8R8A8 UNORM / SRGB
    64: ("a2b10g10r10", 1), 58: ("a2r10g10b10", 1),
    122: ("b10g11r11", 1),
}
REGIONS = ("sky", "aircraft", "near", "far")


def cast(data, code):
    try:
        return memoryview(data).cast(code)
    except (TypeError, ValueError):
        size = struct.calcsize("<" + code)
        return [v[0] for v in struct.iter_unpack("<" + code, data[: len(data) // size * size])]


def unpack_f11(bits, mantissa_bits):
    exponent = bits >> mantissa_bits
    mantissa = bits & ((1 << mantissa_bits) - 1)
    if exponent == 0:
        return mantissa / (1 << mantissa_bits) * 2.0 ** -14
    if exponent == 31:
        return 65504.0
    return (1 + mantissa / (1 << mantissa_bits)) * 2.0 ** (exponent - 15)


def luminance(meta, block):
    """Brightness per pixel, HDR values compressed with x/(1+x) so highlights
    do not dominate. The same mapping is applied to both frames."""
    kind, _ = FORMAT_CHANNELS.get(meta["color_format"], (None, 0))
    count = meta["width"] * meta["height"]
    weights = (0.2126, 0.7152, 0.0722)
    out = [0.0] * count
    if kind in ("half", "float"):
        values = cast(block, "e" if kind == "half" else "f")
        for i in range(count):
            r, g, b = values[4 * i], values[4 * i + 1], values[4 * i + 2]
            y = weights[0] * r + weights[1] * g + weights[2] * b
            y = 0.0 if not math.isfinite(y) or y < 0 else y
            out[i] = y / (1.0 + y)
    elif kind in ("rgba8", "bgra8"):
        swap = kind == "bgra8"
        for i in range(count):
            r, g, b = block[4 * i], block[4 * i + 1], block[4 * i + 2]
            if swap:
                r, b = b, r
            out[i] = (weights[0] * r + weights[1] * g + weights[2] * b) / 255.0
    elif kind in ("a2b10g10r10", "a2r10g10b10"):
        words = cast(block, "I")
        for i in range(count):
            w = words[i]
            first, second, third = w & 1023, (w >> 10) & 1023, (w >> 20) & 1023
            r, g, b = (first, second, third) if kind == "a2b10g10r10" else (third, second, first)
            out[i] = (weights[0] * r + weights[1] * g + weights[2] * b) / 1023.0
    elif kind == "b10g11r11":
        words = cast(block, "I")
        for i in range(count):
            w = words[i]
            r = unpack_f11(w & 2047, 6)
            g = unpack_f11((w >> 11) & 2047, 6)
            b = unpack_f11((w >> 22) & 1023, 5)
            y = weights[0] * r + weights[1] * g + weights[2] * b
            out[i] = y / (1.0 + y)
    else:
        raise ValueError(f"Unsupported colour format {meta['color_format']}")
    return out


def load(folder):
    folder = Path(folder)
    meta = json.loads((folder / "capture.json").read_text(encoding="utf-8"))
    if meta.get("format") != "xp-motion-capture" or meta.get("version") != 1:
        raise ValueError("Not a version 1 motion capture")
    if meta.get("motion_contract") != 1:
        raise ValueError("Capture uses an unknown motion contract")
    w, h, layout = meta["width"], meta["height"], meta["layout"]
    if not (0 < w <= 4096 and 0 < h <= 4096) or len(meta["frames"]) != 2:
        raise ValueError("Unexpected capture size")
    frames = []
    for entry in meta["frames"]:
        data = (folder / entry["file"]).read_bytes()
        if len(data) != layout["bytes"]:
            raise ValueError(f"{entry['file']} has the wrong size")
        count = w * h
        color = data[layout["color"]: layout["color"] + count * meta["color_bytes"]]
        frames.append({
            "meta": entry,
            "luma": luminance(meta, color),
            "depth": cast(data[layout["depth"]: layout["depth"] + 4 * count], "f"),
            "motion": cast(data[layout["motion"]: layout["motion"] + 4 * count], "e"),
            "reactive": data[layout["reactive"]: layout["reactive"] + count],
        })
    return meta, frames


def bilinear(values, w, h, x, y):
    """Sample at continuous pixel coordinates (centre convention)."""
    x -= 0.5
    y -= 0.5
    if x < 0 or y < 0 or x > w - 1 or y > h - 1:
        return None
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    top = values[y0 * w + x0] * (1 - fx) + values[y0 * w + x1] * fx
    bottom = values[y1 * w + x0] * (1 - fx) + values[y1 * w + x1] * fx
    return top * (1 - fy) + bottom * fy


def distance(meta, d):
    depth = meta["depth"]
    near, far = depth["near"], depth["far"]
    if near <= 0:
        return math.inf
    if depth["reverse_z"]:
        if depth["infinite"] or far <= near:
            return near / d if d > 0 else math.inf
        return near * far / (d * (far - near) + near)
    return near * far / max(1e-12, far - d * (far - near))


def measure(meta, frames, step=2, heat=None):
    w, h = meta["width"], meta["height"]
    previous, current = frames
    jx0, jy0 = previous["meta"]["jitter_x"], previous["meta"]["jitter_y"]
    jx1, jy1 = current["meta"]["jitter_x"], current["meta"]["jitter_y"]
    reverse = meta["depth"]["reverse_z"]
    stats = {name: {"samples": 0, "error": 0.0, "baseline": 0.0, "motion": 0.0, "errors": []}
             for name in REGIONS}
    invalid = outside = total = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            i = y * w + x
            total += 1
            r = current["reactive"][i] / 255.0
            mx, my = current["motion"][2 * i], current["motion"][2 * i + 1]
            d = current["depth"][i]
            if r >= 0.999 or not (math.isfinite(mx) and math.isfinite(my) and math.isfinite(d)):
                invalid += 1
                continue
            px, py = x + 0.5 - jx1 + mx + jx0, y + 0.5 - jy1 + my + jy0
            bx, by = x + 0.5 - jx1 + jx0, y + 0.5 - jy1 + jy0
            moved = bilinear(previous["luma"], w, h, px, py)
            still = bilinear(previous["luma"], w, h, bx, by)
            if moved is None or still is None:
                outside += 1
                continue
            sky = d < 1e-7 if reverse else d > 0.9999999
            region = ("sky" if sky else "aircraft" if r > 0.01 else
                      "near" if distance(meta, d) < 100.0 else "far")
            value = current["luma"][i]
            error = abs(value - moved)
            s = stats[region]
            s["samples"] += 1
            s["error"] += error
            s["baseline"] += abs(value - still)
            s["motion"] += math.hypot(mx, my)
            s["errors"].append(error)
            if heat is not None:
                heat[i] = error
    report = {"width": w, "height": h, "step": step, "sampled": total,
              "invalid_percent": 100.0 * invalid / max(1, total),
              "outside_percent": 100.0 * outside / max(1, total),
              "sources": [f["meta"]["source"] for f in frames], "regions": {}}
    all_error = all_baseline = 0.0
    all_samples = 0
    for name in REGIONS:
        s = stats[name]
        n = s["samples"]
        if not n:
            continue
        s["errors"].sort()
        mean, base = s["error"] / n, s["baseline"] / n
        report["regions"][name] = {
            "samples": n,
            "mean_error": mean,
            "baseline_error": base,
            "gain_percent": 100.0 * (1.0 - mean / base) if base > 1e-9 else 0.0,
            "p95_error": s["errors"][min(n - 1, int(0.95 * n))],
            "mean_motion_px": s["motion"] / n,
        }
        all_error += s["error"]
        all_baseline += s["baseline"]
        all_samples += n
    report["overall"] = {
        "samples": all_samples,
        "mean_error": all_error / max(1, all_samples),
        "baseline_error": all_baseline / max(1, all_samples),
        "gain_percent": 100.0 * (1.0 - all_error / all_baseline) if all_baseline > 1e-9 else 0.0,
    }
    motion = sum(r["mean_motion_px"] * r["samples"] for r in report["regions"].values())
    report["mean_motion_px"] = motion / max(1, all_samples)
    report["warning"] = ("The camera barely moved between the two frames; pan or turn "
                         "during the capture to test motion." if report["mean_motion_px"] < 0.25 else "")
    return report


def write_heatmap(path, w, h, heat):
    import binascii
    import zlib
    peak = max([v for v in heat if v is not None] + [1e-6])
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            v = heat[y * w + x]
            if v is None:
                row += b"\x20\x20\x60"  # not measured: dark blue
            else:
                level = int(255 * min(1.0, math.sqrt(v / peak)))
                row += bytes((level, level, level))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(b"\0" + r for r in rows))) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def summary(report):
    lines = [f"Motion quality ({report['width']}x{report['height']} crop, every {report['step']} px; "
             f"source {report['sources'][1]})"]
    for name, r in report["regions"].items():
        lines.append(f"  {name:9s} gain {r['gain_percent']:6.1f}%  error {r['mean_error']:.4f} "
                     f"(no motion {r['baseline_error']:.4f})  p95 {r['p95_error']:.4f}  "
                     f"motion {r['mean_motion_px']:.2f}px  samples {r['samples']}")
    o = report["overall"]
    lines.append(f"  overall   gain {o['gain_percent']:6.1f}%  error {o['mean_error']:.4f}  "
                 f"invalid {report['invalid_percent']:.1f}%  left the crop {report['outside_percent']:.1f}%")
    if report["warning"]:
        lines.append("  NOTE: " + report["warning"])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", type=Path)
    parser.add_argument("--step", type=int, default=2, help="sample every Nth pixel (1-8)")
    parser.add_argument("--json", type=Path, help="also write the report as JSON")
    parser.add_argument("--heatmap", type=Path, help="write a PNG of per-pixel error")
    args = parser.parse_args(argv)
    step = max(1, min(8, args.step))
    meta, frames = load(args.capture)
    heat = [None] * (meta["width"] * meta["height"]) if args.heatmap else None
    report = measure(meta, frames, step, heat)
    print(summary(report))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if args.heatmap:
        write_heatmap(args.heatmap, meta["width"], meta["height"], heat)
    return 0


if __name__ == "__main__":
    sys.exit(main())
