"""Regression checks for HDR evidence preservation, not simulator image quality.

SPDX-License-Identifier: MIT
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PACKAGES = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None
if PACKAGES:
    sys.path.insert(0, str(PACKAGES))
import numpy as np
from PIL import Image
from convert_scrgb_capture import convert
from capture_evidence import public_provenance, sha, verify_hdr_comparison


class HdrCaptureTests(unittest.TestCase):
    def test_release_provenance_excludes_private_capture_metadata(self):
        private = {"movie_sha256":"approved-video", "seconds":98, "frames":2947,
            "project":{"version":"0.6.3-alpha", "voice":"bm_george", "speed":1,
                "scenes":[{"narration":"Choose a preset."}],
                "capture":{"simulator":"X-Plane 12.4.4", "resolution":[1920,1080],
                    "evidence":"PRIVATE-FILENAME", "gpu":"PRIVATE-HARDWARE",
                    "note":"PRIVATE-NOTE"}},
            "hdr_capture_evidence":{"white_nits":"PRIVATE-CALIBRATION",
                "states":{"after":{"master":"PRIVATE-MASTER"}}},
            "capture_sha256":{"PRIVATE-RECORDING":"localhash"}}
        result = public_provenance(private)
        self.assertNotIn("PRIVATE-", json.dumps(result))
        self.assertEqual(result["movie_sha256"], "approved-video")
        self.assertEqual(result["project"]["scenes"][0]["narration"], "Choose a preset.")
        self.assertEqual(result["project"]["capture"]["resolution"], [1920,1080])

    def test_hdr_ramp_keeps_highlight_detail(self):
        # Values above scRGB 1 must stay distinct, unlike the old 8-bit path.
        ramp = np.linspace(0, 430/80, 512, dtype=np.float32)
        encoded = convert(np.repeat(ramp[:, None], 3, axis=1), 240, 430, np)
        self.assertTrue(np.all(np.diff(encoded[:, 0]) > 0))
        np.testing.assert_allclose(encoded[0], 0, atol=1e-7)
        np.testing.assert_allclose(encoded[-1], 1, atol=1e-6)
        self.assertLess(encoded[100, 0], encoded[300, 0])

    def test_wide_gamut_and_invalid_input(self):
        output = convert(np.array([[-.5, 5, .2], [3, 1, 8]], np.float32), 240, 430, np)
        self.assertTrue(np.isfinite(output).all())
        self.assertTrue(((output >= 0) & (output <= 1)).all())
        for white, peak in [(0, 430), (240, 80), (float("nan"), 430), (240, float("inf"))]:
            with self.assertRaises(ValueError):
                convert(np.ones((1,3)), white, peak, np)
        with self.assertRaises(ValueError):
            convert(np.array([[float("nan"), 0, 1]]), 240, 430, np)

    def test_packed_fp16_cli_does_not_clip_or_overwrite(self):
        if not PACKAGES:
            self.skipTest("Pass the build packages directory to test the CLI")
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            raw, png = folder/"capture.rgba16f", folder/"capture.png"
            np.array([[[[0,0,0,1], [0,0,0,1]]],
                      [[[1,1,1,1], [3,3,3,1]]]], dtype="<f2").tofile(raw)
            cmd = [sys.executable, str(Path(__file__).with_name("convert_scrgb_capture.py")),
                   str(raw), str(png), "--packages", str(PACKAGES), "--width", "2", "--height", "1",
                   "--white-nits", "240", "--peak-nits", "430"]
            subprocess.run(cmd, check=True, capture_output=True)
            report = json.loads(png.with_suffix(".color.json").read_text())
            self.assertEqual(report["rgb_max"], 3)
            self.assertEqual(report["selected_frame"], 1)
            self.assertGreater(np.array(Image.open(png))[0,1,0], np.array(Image.open(png))[0,0,0])
            self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)

    def test_evidence_rejects_changed_capture_or_exposure(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            manifest = {"schema":1, "format":"fp16_scrgb_to_sdr", "white_nits":240,
                        "peak_nits":430, "states":{}, "sha256":{}}
            for n,state in enumerate(("before", "after")):
                item = {key:state+ext for key,ext in [("image",".png"),("master",".rgba16f"),
                                                     ("color",".json"),("status",".txt")]}
                manifest["states"][state] = item
                (folder/item["image"]).write_bytes(b"test image")
                (folder/item["master"]).write_bytes(b"test master")
                (folder/item["status"]).write_text(
                    ("Disabled; native" if n==0 else "Native AA recorded") +
                    f"\n3.1.6\n1 1 1 1920 1080 1920 1080 {n}\n\n0 0 0 0\n")
                color = {"source_sha256":sha(folder/item["master"]),
                         "output_sha256":sha(folder/item["image"]), "width":1920,"height":1080,
                         "white_nits":240,"peak_nits":430,"selective_edits":False,
                         "renderer_artifact_repair":False,
                         "source_format":"RGBA FP16 little-endian; linear scRGB; 1.0 = 80 nit"}
                (folder/item["color"]).write_text(json.dumps(color))
                for name in item.values(): manifest["sha256"][name] = sha(folder/name)
            def save(): (folder/"manifest.json").write_text(json.dumps(manifest))
            save()
            verify_hdr_comparison(folder, "manifest.json")
            (folder/"after.png").write_bytes(b"different")
            with self.assertRaises(ValueError): verify_hdr_comparison(folder, "manifest.json")
            (folder/"after.png").write_bytes(b"test image")
            color["white_nits"] = 160
            (folder/"after.json").write_text(json.dumps(color))
            manifest["sha256"]["after.json"] = sha(folder/"after.json")
            save()
            with self.assertRaises(ValueError): verify_hdr_comparison(folder, "manifest.json")


if __name__ == "__main__":
    unittest.main()
