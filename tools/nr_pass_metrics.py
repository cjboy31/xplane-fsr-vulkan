"""Measure retained input texture in NRCheck --pass-study outputs; no quality certification."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path


def measure(folder):
    folder = Path(folder)
    meta = json.loads((folder / "NR-PASS-STUDY.json").read_text())
    width, height = meta["width"], meta["height"]
    if not (64 <= width <= 8192 and 64 <= height <= 8192):
        raise ValueError("Unsupported study extent")
    planes = []
    for name in ("NR-INPUT.rgba32f", "NR-LAST.rgba32f"):
        data = (folder / name).read_bytes()
        if len(data) != width * height * 16:
            raise ValueError("Float artifact size does not match its metadata")
        samples = array.array("f")
        samples.frombytes(data)
        if not all(math.isfinite(v) for v in samples):
            raise ValueError("Non-finite study data")
        planes.append([samples[i] * .2126 + samples[i+1] * .7152 + samples[i+2] * .0722
                       for i in range(0, len(samples), 4)])
    source, result = planes
    # Fixed textured region, excluding the fixture's HDR light and borders.
    region = [y * width + x for y in range(16, height-16)
              for x in range(16, width*3//4-16)]
    mean_source = sum(source[i] for i in region) / len(region)
    mean_result = sum(result[i] for i in region) / len(region)
    if min(mean_source, mean_result) <= 0:
        raise ValueError("Texture region has no positive luminance")
    scale = mean_source / mean_result
    report = dict(meta, case=folder.name, meanLumaRatio=mean_result/mean_source,
                  inputSha256=hashlib.sha256((folder/"NR-INPUT.rgba32f").read_bytes()).hexdigest())
    for step in (1, 2, 4, 8):
        aa = bb = ab = 0.
        for i in region:
            a = source[i] - sum(source[j] for j in (i-step, i+step, i-step*width, i+step*width))*.25
            b = (result[i] - sum(result[j] for j in (i-step, i+step, i-step*width, i+step*width))*.25)*scale
            aa += a*a
            bb += b*b
            ab += a*b
        if min(aa, bb) <= 0:
            raise ValueError("Texture region has no measurable detail")
        report["detail"+str(step)] = dict(coherentGain=ab/aa,
            correlation=ab/math.sqrt(aa*bb), energyRatio=math.sqrt(bb/aa))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reports = [measure(p) for p in args.directories]
    # A matched comparison must use identical raw input, even across pass counts.
    if len({r["inputSha256"] for r in reports}) != 1:
        raise ValueError("Inputs differ: compare matching format/fixture captures separately")
    text = json.dumps({"syntheticOnly": True, "comparisons": reports}, indent=2)
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text)
