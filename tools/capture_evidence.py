"""Verify locally retained HDR comparison evidence before rendering review media.

SPDX-License-Identifier: MIT
"""
import hashlib
import json
from pathlib import Path


def public_provenance(private):
    """Keep release credits/integrity without local calibration or evidence IDs."""
    keys = ("seconds", "frames", "fps", "movie_sha256", "voice", "speech_models",
            "narration_sha256", "transition", "reused_narration_sha256", "comparison_policy")
    result = {key: private[key] for key in keys if key in private}
    source = private["project"]
    result["project"] = {key: source[key] for key in
                         ("version", "voice", "speed", "scenes") if key in source}
    capture = source["capture"]
    result["project"]["capture"] = {key: capture[key] for key in
        ("simulator", "renderer_version", "aircraft", "resolution", "comparison", "crop", "caption")
        if key in capture}
    result["capture_sha256"] = {}
    result["capture_method"] = "Real sequential captures; identical HDR-to-SDR conversion and crop."
    if capture.get("background_only"):
        result["project"]["capture"] = {"background_only": True}
        result["capture_method"] = "Previously reviewed background still; no new comparison or FG demonstration."
    result["publication_state"] = "Rendered; follow release workflow"
    return result


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def verify_hdr_comparison(folder, manifest_name):
    folder = Path(folder).resolve()

    def local(name):
        path = (folder / name).resolve()
        if path.parent != folder:
            raise ValueError("Comparison evidence must be in its capture folder")
        return path

    manifest = json.loads(local(manifest_name).read_text(encoding="utf-8"))
    if manifest["schema"] != 1 or manifest["format"] != "fp16_scrgb_to_sdr":
        raise ValueError("Unsupported comparison evidence format")
    for name, digest in manifest["sha256"].items():
        if sha(local(name)) != digest:
            raise ValueError("Comparison evidence hash mismatch: " + name)
    result = {}
    for state in ("before", "after"):
        item = manifest["states"][state]
        for name in item.values():
            if name not in manifest["sha256"]:
                raise ValueError("Unverified comparison file: " + name)
        color = json.loads(local(item["color"]).read_text(encoding="utf-8"))
        if (color["source_sha256"] != manifest["sha256"][item["master"]] or
                color["output_sha256"] != manifest["sha256"][item["image"]] or
                (color["width"], color["height"]) != (1920, 1080) or
                (color["white_nits"], color["peak_nits"]) !=
                (manifest["white_nits"], manifest["peak_nits"]) or
                color["selective_edits"] is not False or
                color["renderer_artifact_repair"] is not False or
                color["source_format"] != "RGBA FP16 little-endian; linear scRGB; 1.0 = 80 nit"):
            raise ValueError("Mismatched or retouched HDR comparison")
        status = local(item["status"]).read_text(encoding="utf-8-sig").splitlines()
        expected = 0 if state == "before" else 1
        if (not status[0].startswith("Disabled" if state == "before" else "Native AA recorded") or
                status[1] != "3.1.6" or
                list(map(int, status[2].split()[3:])) != [1920,1080,1920,1080,expected] or
                list(map(int, status[4].split())) != [0,0,0,0]):
            raise ValueError("Unconfirmed comparison processing/FG state")
        result[state] = local(item["image"])
    return result, manifest
