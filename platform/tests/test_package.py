"""Native archive allow-list, integrity and install/update/uninstall round trip.
SPDX-License-Identifier: MIT
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def main(archive):
    with tempfile.TemporaryDirectory() as temporary:
        # macOS exposes /var through /private/var; compare canonical paths.
        destination = Path(temporary).resolve()
        if archive.name.endswith(".tar.gz"):
            with tarfile.open(archive) as source:
                for member in source.getmembers():
                    assert not member.issym() and not member.islnk()
                    assert (destination / member.name).resolve().is_relative_to(destination)
                source.extractall(destination, filter="data")
        else:
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    assert (destination / member.filename).resolve().is_relative_to(destination)
                source.extractall(destination)
        package, = destination.iterdir()
        info = json.loads((package / "BUILD-INFO.json").read_text())
        system = info["platform"]
        linux = system == "Linux"
        allowed = {"core.py", "launcher.py", "LICENSE.txt", "INSTALLATION-AND-USE.txt",
                   "licenses/X-Plane-SDK.txt", "BUILD-INFO.json", "PAYLOAD-SHA256SUMS.txt",
                   "Launch-FSR" if linux else "Launch.command", "probe/64/lin.xpl" if linux else "probe/64/mac.xpl"}
        if linux:
            allowed |= {"VkLayer_xplane_fsr3.json", "libVkLayer_xplane_fsr3.so",
                        "licenses/JonathanOrr-xplane-fsr3-MIT.txt", "licenses/AMD-FidelityFX-MIT.txt",
                        "licenses/Khronos-Vulkan-Headers-MIT.txt"}
        actual = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()}
        assert actual == allowed, actual ^ allowed
        for line in (package / "PAYLOAD-SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256((package / name).read_bytes()).hexdigest() == digest, name
        assert not any(info[k] for k in ("fsr4", "frame_generation", "mac_fsr_rendering", "live_flight_validated"))
        sys.path.insert(0, str(package))
        import core
        root = destination / "user-choices"
        choices = dict(core.DEFAULTS, sim="test simulator", mode="Compare: X-Plane FSR 1", arguments="--fps_test=1")
        core.save_settings(root, choices)
        sim = destination / "sim"
        executable = sim / ("X-Plane-x86_64" if linux else "X-Plane.app/Contents/MacOS/X-Plane")
        executable.parent.mkdir(parents=True)
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        installed = core.install_probe(package, sim, system)
        keep = installed / "user-note.txt"
        keep.write_text("preserve me")
        core.install_probe(package, sim, system)
        core.uninstall_probe(sim)
        assert keep.read_text() == "preserve me"
        assert core.load_settings(root) == choices
        help_result = subprocess.run([sys.executable, str(package / "launcher.py"), "--help"], capture_output=True, text=True)
        assert help_result.returncode == 0 and "No FSR 4" in help_result.stdout
        if linux:
            manifest = json.loads((package / "VkLayer_xplane_fsr3.json").read_text())
            assert manifest["layer"]["library_path"] == "./libVkLayer_xplane_fsr3.so"
            assert os.access(package / "Launch-FSR", os.X_OK)
        print("PASS native archive allow-list, payload hashes, help, owned install/update/uninstall, settings preservation")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
