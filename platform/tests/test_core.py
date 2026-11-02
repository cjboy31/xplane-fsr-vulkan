"""Behavioral checks for platform isolation, persisted choices and probe ownership."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "native"))
import core


class NativeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.package = self.root / "package with spaces"
        self.package.mkdir()
        (self.package / "libVkLayer_xplane_fsr3.so").write_bytes(b"test backend")

    def tearDown(self):
        self.temporary.cleanup()

    def test_choices_survive_reopen_and_update(self):
        saved = dict(core.DEFAULTS, sim="/home/test/My X-Plane", mode="Diagnostic: observe only", arguments='--name="a b"')
        core.save_settings(self.root, saved)
        self.assertEqual(core.load_settings(self.root), saved)
        saved["sim"] = "/different installation"
        core.save_settings(self.root, saved)
        self.assertEqual(core.load_settings(self.root)["mode"], "Diagnostic: observe only")
        self.assertFalse(list(self.root.glob(".xpfsr-*")))

    def test_simulator_found_from_x_plane_install_list(self):
        home = self.root / "home"
        good = self.root / "X Plane 12"
        good.mkdir()
        executable = good / "X-Plane-x86_64"
        executable.write_text("#!/bin/sh\n")
        executable.chmod(0o755)
        (home / ".x-plane").mkdir(parents=True)
        listing = home / ".x-plane/x-plane_install_12.txt"
        listing.write_text(f"/missing/x-plane\n\n{good}\n", encoding="utf-8")
        env = {"HOME": str(home)}
        self.assertEqual(core.detect_simulator("Linux", env), str(good.resolve()))
        self.assertEqual(core.install_list("Darwin", env), home / "Library/Preferences/x-plane_install_12.txt")
        self.assertEqual(core.detect_simulator("Darwin", env), "")  # Mac list absent
        listing.write_text("/missing/x-plane\n", encoding="utf-8")
        self.assertEqual(core.detect_simulator("Linux", env), "")
        listing.unlink()
        self.assertEqual(core.detect_simulator("Linux", env), "")

    def test_corrupt_settings_not_silently_reset(self):
        path = self.root / "settings.json"
        path.write_text("{broken")
        with self.assertRaises(ValueError):
            core.load_settings(self.root)
        self.assertEqual(path.read_text(), "{broken")

    def test_environment_is_child_only_and_fg_off(self):
        inherited = {"PATH": "/bin", "VK_INSTANCE_LAYERS": "OTHER:VK_LAYER_XPLANE_fsr3",
                     "VK_ADD_LAYER_PATH": "/other", "XP_FSR3_AB": "1", "XP_FSR3_FP16": "1",
                     "XPFSR24_FRAMEGEN": "4", "XPFSR24_MODE": "fsr4"}
        original = inherited.copy()
        env = core.launch_environment(self.package, self.root, "FSR 3.1 upscaling", inherited)
        self.assertEqual(inherited, original)
        self.assertEqual(env["VK_INSTANCE_LAYERS"], core.LAYER + ":OTHER")
        self.assertEqual(env["VK_ADD_LAYER_PATH"], str(self.package.resolve()) + ":/other")
        self.assertEqual(env["XPFSR24_FRAMEGEN"], "0")
        self.assertNotIn("XP_FSR3_AB", env)
        self.assertNotIn("XP_FSR3_FP16", env)
        self.assertNotIn("XPFSR24_MODE", env)

    def test_mac_never_enables_vulkan(self):
        env = core.launch_environment(self.root, self.root, "FSR 3.1 upscaling", {}, "Darwin")
        self.assertNotIn("VK_INSTANCE_LAYERS", env)
        self.assertNotIn("XP_FSR3", env)

    def test_compare_removes_inherited_own_layer(self):
        env = core.launch_environment(self.package, self.root, "Compare: X-Plane FSR 1",
                                      {"VK_INSTANCE_LAYERS": core.LAYER + ":OTHER"})
        self.assertEqual(env["VK_INSTANCE_LAYERS"], "OTHER")
        self.assertNotIn("XP_FSR3", env)

    def test_sessions_cannot_reuse_stale_active(self):
        first, second = core.create_session(self.root), core.create_session(self.root)
        (first / "status.txt").write_text("ACTIVE")
        self.assertNotIn("ACTIVE", core.renderer_status(second, True, "Linux"))
        self.assertIn("ENDED", core.renderer_status(first, False, "Linux"))
        os.utime(first / "status.txt", (time.time() - 60,) * 2)
        self.assertIn("NO RECENT", core.renderer_status(first, True, "Linux"))

    def test_probe_update_removal_preserve_choices_and_foreign_files(self):
        sim = self.root / "sim"
        sim.mkdir()
        executable = sim / "X-Plane-x86_64"
        executable.write_text("test")
        executable.chmod(0o755)
        source = self.package / "probe/64/lin.xpl"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"probe1")
        with patch.object(os, "access", return_value=True):
            target = core.install_probe(self.package, sim, "Linux")
            source.write_bytes(b"probe2")
            core.install_probe(self.package, sim, "Linux")
        self.assertEqual((target / "64/lin.xpl").read_bytes(), b"probe2")
        keep = target / "user-note.txt"
        keep.write_text("keep")
        core.uninstall_probe(sim)
        self.assertEqual(keep.read_text(), "keep")
        self.assertFalse((target / "64/lin.xpl").exists())
        with self.assertRaises(ValueError):
            core.install_probe(self.package, sim, "Linux")

    @unittest.skipIf(os.name == "nt", "POSIX symbolic-link behavior")
    def test_probe_update_rejects_symbolic_links(self):
        sim = self.root / "sim"
        sim.mkdir()
        executable = sim / "X-Plane-x86_64"
        executable.write_text("test")
        executable.chmod(0o755)
        source = self.package / "probe/64/lin.xpl"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"new")
        target = core.install_probe(self.package, sim, "Linux")
        binary = target / "64/lin.xpl"
        binary.unlink()
        protected = self.root / "protected.txt"
        protected.write_text("keep")
        binary.symlink_to(protected)
        with self.assertRaises(ValueError):
            core.install_probe(self.package, sim, "Linux")
        with self.assertRaises(ValueError):
            core.uninstall_probe(sim)
        self.assertEqual(protected.read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
