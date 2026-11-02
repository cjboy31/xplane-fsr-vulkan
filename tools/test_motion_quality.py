"""Self-test for tools/motion_quality.py with synthetic captures (original code).

SPDX-License-Identifier: MIT
Builds two-frame captures of a textured pan with known motion and jitter, in the
exact layout the layer writes, and checks that correct vectors score high, zero
vectors score near zero and wrong-direction vectors score below zero.
"""
import json
import math
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import motion_quality  # noqa: E402

W, H = 96, 80
PAN = (3.25, -1.5)          # content moved: previous position minus current
JITTER = ((0.21, -0.33), (-0.17, 0.29))


def texture(u, v):
    # A gradient plus detail: the error grows with the offset, so vectors that
    # point the wrong way must score worse than no motion at all.
    return 0.15 + 0.003 * u + 0.002 * v + 0.05 * math.sin(u * 0.21) * math.cos(v * 0.17)


def frame_bytes(index, motion, sky_rows=8, cabin=False):
    jx, jy = JITTER[index]
    # Frame 1 is current. Content at unjittered current position q was at q + PAN
    # in frame 0, so frame 0 shows texture(q) at q + PAN.
    offset = (0.0, 0.0) if index == 1 else PAN
    color, depth, vectors, reactive = bytearray(), bytearray(), bytearray(), bytearray()
    for y in range(H):
        for x in range(W):
            qx, qy = x + 0.5 - jx - offset[0], y + 0.5 - jy - offset[1]
            value = texture(qx, qy)
            linear = value / (1 - value)  # tool maps x/(1+x) back to value
            color += struct.pack("<4e", linear, linear, linear, 1.0)
            sky = y < sky_rows
            depth += struct.pack("<f", 0.0 if sky else 0.0001)
            vectors += struct.pack("<2e", *motion)
            reactive += bytes((13 if cabin and x < 10 else 0,))
    return color, depth, vectors, reactive


def write_capture(folder, motion, cabin=False):
    folder.mkdir()
    parts0 = frame_bytes(0, motion, cabin=cabin)
    parts1 = frame_bytes(1, motion, cabin=cabin)
    count = W * H

    def align(v):
        return (v + 255) & ~255
    layout = {"color": 0}
    layout["depth"] = align(count * 8)
    layout["motion"] = align(layout["depth"] + count * 4)
    layout["reactive"] = align(layout["motion"] + count * 4)
    layout["bytes"] = layout["reactive"] + count
    for name, parts in (("frame0.bin", parts0), ("frame1.bin", parts1)):
        blob = bytearray(layout["bytes"])
        for key, part in zip(("color", "depth", "motion", "reactive"), parts):
            blob[layout[key]: layout[key] + len(part)] = part
        (folder / name).write_bytes(bytes(blob))
    meta = {
        "format": "xp-motion-capture", "version": 1, "motion_contract": 1,
        "width": W, "height": H, "crop_x": 0, "crop_y": 0, "render_width": W, "render_height": H,
        "color_format": 97, "color_bytes": 8,
        "depth": {"reverse_z": 1, "infinite": 1, "near": 0.05, "far": 0},
        "layout": layout,
        "frames": [
            {"file": "frame0.bin", "camera_frame": 10, "jitter_x": JITTER[0][0], "jitter_y": JITTER[0][1],
             "reset": False, "source": "camera"},
            {"file": "frame1.bin", "camera_frame": 11, "jitter_x": JITTER[1][0], "jitter_y": JITTER[1][1],
             "reset": False, "source": "camera"},
        ],
    }
    (folder / "capture.json").write_text(json.dumps(meta), encoding="utf-8")


def run(folder):
    meta, frames = motion_quality.load(folder)
    return motion_quality.measure(meta, frames, step=1)


def main():
    failures = []

    def expect(ok, what):
        if not ok:
            failures.append(what)
            print("FAIL:", what)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        write_capture(root / "right", PAN, cabin=True)
        write_capture(root / "zero", (0.0, 0.0))
        write_capture(root / "wrong", (-PAN[0], -PAN[1]))
        right, zero, wrong = run(root / "right"), run(root / "zero"), run(root / "wrong")
        expect(right["regions"]["far"]["gain_percent"] > 90, f"correct vectors gain {right['regions']['far']['gain_percent']:.1f}%")
        expect(abs(zero["overall"]["gain_percent"]) < 1e-6, "zero vectors equal the baseline")
        expect(wrong["overall"]["gain_percent"] < 0, "wrong-direction vectors score below zero")
        expect("sky" in right["regions"] and "aircraft" in right["regions"], "sky and marked cabin regions reported")
        expect(abs(right["mean_motion_px"] - math.hypot(*PAN)) < 0.01, "mean motion magnitude")
        expect(zero["warning"] != "" and right["warning"] == "", "still-camera warning")
        heat = [None] * (W * H)
        motion_quality.measure(*motion_quality.load(root / "right"), step=1, heat=heat)
        motion_quality.write_heatmap(root / "heat.png", W, H, heat)
        expect((root / "heat.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n", "heatmap PNG written")
        broken = root / "right" / "frame1.bin"
        broken.write_bytes(broken.read_bytes()[:-1])
        try:
            motion_quality.load(root / "right")
            expect(False, "truncated frame accepted")
        except ValueError:
            pass
        print(motion_quality.summary(right))
    if failures:
        return 1
    print("PASS: motion-quality tool scores correct, zero and wrong vectors as expected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
