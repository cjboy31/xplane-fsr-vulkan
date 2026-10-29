#!/usr/bin/env python3
"""Build native previews on Linux/macOS hosts. Dependencies are pinned, never committed.
SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.6.2-platform-preview.1"
UPSTREAM = "fa51577c9bc2c3477e64dfdd358796aebb9ca45b"
VULKAN_HEADERS = "cde2207d8bc426cfd201ffd67031e7b135a01df7"
SDK_SHA = "73e6d8be3b0b3bf1fd703113e0b0df2aa854db753c66802d7379320dbc22f771"
SDK_URL = "https://github.com/X-Plane/xpsdk/releases/download/xpsdk_4.4.0-r1/XPSDK440-r1.zip"


def run(*args, cwd=ROOT, env=None):
    print("+", *map(str, args), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fetch_sdk(work):
    archive = work / "XPSDK440-r1.zip"
    if not archive.exists():
        urllib.request.urlretrieve(SDK_URL, archive)
    if sha(archive) != SDK_SHA:
        raise RuntimeError("Official SDK archive failed its pinned SHA-256 check")
    destination = work / "xpsdk-440-r1"
    if not destination.exists():
        with zipfile.ZipFile(archive) as source:
            for member in source.infolist():
                resolved = (destination / member.filename).resolve()
                if not resolved.is_relative_to(destination.resolve()):
                    raise RuntimeError("Unsafe SDK archive member")
            source.extractall(destination)
    headers = list(destination.rglob("CHeaders/XPLM/XPLMPlugin.h"))
    if len(headers) != 1:
        raise RuntimeError("Unexpected SDK layout")
    return headers[0].parents[2]


def package(output, system, layer, probe, sdk, upstream):
    name = f"XPlane-FSR-Native-{VERSION}-" + ("Linux-x64" if system == "Linux" else "Mac-Universal-Probe")
    target = output / name
    if target.exists():
        raise RuntimeError("Package folder already exists; choose a fresh --output folder")
    target.mkdir(parents=True)
    for name in ("core.py", "launcher.py"):
        shutil.copy2(ROOT / "platform/native" / name, target / name)
    shutil.copy2(ROOT / "LICENSE", target / "LICENSE.txt")
    shutil.copy2(ROOT / "platform/INSTALLATION-AND-USE.txt", target / "INSTALLATION-AND-USE.txt")
    license_dir = target / "licenses"
    license_dir.mkdir()
    shutil.copy2(sdk / "license.txt", license_dir / "X-Plane-SDK.txt")
    binary = target / "probe/64" / ("lin.xpl" if system == "Linux" else "mac.xpl")
    binary.parent.mkdir(parents=True)
    shutil.copy2(probe, binary)
    binary.chmod(0o755)
    if layer:
        shutil.copy2(layer, target / layer.name)
        run("strip", "--strip-unneeded", target / layer.name)
        manifest = json.loads((layer.parent / "VkLayer_xplane_fsr3.json").read_text())
        manifest["layer"]["library_path"] = "./" + layer.name
        (target / "VkLayer_xplane_fsr3.json").write_text(json.dumps(manifest, indent=2) + "\n")
        shutil.copy2(upstream / "LICENSE", license_dir / "JonathanOrr-xplane-fsr3-MIT.txt")
        shutil.copy2(upstream / "THIRD_PARTY_NOTICES.md", license_dir / "AMD-FidelityFX-MIT.txt")
        headers = ROOT / "work/native/Vulkan-Headers"
        shutil.copy2(headers / "LICENSES/MIT.txt", license_dir / "Khronos-Vulkan-Headers-MIT.txt")
        run(sys.executable, upstream / "fsr3-layer/tests/check_portable.py", target / layer.name)
    command = target / ("Launch.command" if system == "Darwin" else "Launch-FSR")
    command.write_text('#!/bin/sh\ncd -- "$(dirname -- "$0")" || exit 1\nexec python3 ./launcher.py "$@"\n')
    command.chmod(0o755)
    (target / "BUILD-INFO.json").write_text(json.dumps({
        "version": VERSION, "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "platform": system, "linux_backend_commit": UPSTREAM if layer else None,
        "xplane_sdk_sha256": SDK_SHA, "fsr4": False, "frame_generation": False,
        "mac_fsr_rendering": False, "live_flight_validated": False}, indent=2) + "\n")
    hashes = [f"{sha(p)}  {p.relative_to(target).as_posix()}" for p in sorted(target.rglob("*")) if p.is_file()]
    (target / "PAYLOAD-SHA256SUMS.txt").write_text("\n".join(hashes) + "\n")
    if system == "Linux":
        archive = output / (target.name + ".tar.gz")
        with tarfile.open(archive, "w:gz") as out:
            out.add(target, arcname=target.name)
    else:
        archive = Path(shutil.make_archive(str(output / target.name), "zip", output, target.name))
    (output / (archive.name + ".sha256")).write_text(f"{sha(archive)}  {archive.name}\n")
    run(sys.executable, ROOT / "platform/tests/test_package.py", archive)
    print("BUILT", archive, sha(archive), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "dist/native")
    args = parser.parse_args()
    system = platform.system()
    if system not in ("Linux", "Darwin") or (system == "Linux" and platform.machine() != "x86_64"):
        parser.error("Build on native x86_64 Linux or macOS; Windows is not a native platform test host.")
    work = ROOT / "work/native"
    work.mkdir(parents=True, exist_ok=True)
    sdk = fetch_sdk(work)
    build = work / "probe-build"
    configure = ["cmake", "-S", ROOT / "platform/probe", "-B", build,
                 "-DCMAKE_BUILD_TYPE=RelWithDebInfo", f"-DXPLANE_SDK={sdk}"]
    if system == "Darwin":
        configure += ["-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"]
    run(*configure)
    run("cmake", "--build", build, "--parallel", "2")
    run("ctest", "--test-dir", build, "--output-on-failure")
    probe = build / ("lin.xpl" if system == "Linux" else "mac.xpl")
    if system == "Darwin":
        run("lipo", probe, "-verify_arch", "arm64", "x86_64")
        run("codesign", "--force", "--sign", "-", probe)
        run("codesign", "--verify", "--strict", probe)
    layer, upstream = None, None
    if system == "Linux":
        headers = work / "Vulkan-Headers"
        if not headers.exists():
            run("git", "clone", "--filter=blob:none", "--no-checkout", "https://github.com/KhronosGroup/Vulkan-Headers.git", headers)
            run("git", "checkout", "--detach", VULKAN_HEADERS, cwd=headers)
        if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=headers, text=True).strip() != VULKAN_HEADERS:
            raise RuntimeError("Vulkan headers are not the reviewed pinned commit")
        upstream = work / "xplane-fsr3"
        if not upstream.exists():
            run("git", "clone", "--filter=blob:none", "--no-checkout", "https://github.com/JonathanOrr/xplane-fsr3.git", upstream)
            run("git", "checkout", "--detach", UPSTREAM, cwd=upstream)
        if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=upstream, text=True).strip() != UPSTREAM:
            raise RuntimeError("Linux dependency is not the reviewed pinned commit")
        if not (upstream / "third_party/FidelityFX-SDK").exists():
            run("bash", upstream / "scripts/fetch-sdk.sh")
        linux_build = work / "linux-build"
        run("cmake", "-S", upstream / "fsr3-layer", "-B", linux_build, "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DXPLANE_ROOT=",
            f"-DVulkan_INCLUDE_DIR={headers / 'include'}")
        run("cmake", "--build", linux_build, "--parallel", "2")
        # Running directly makes a missing software GPU a failure, never a skipped green test.
        env = dict(os.environ, FSR_DEVICE="llvmpipe", FSR_VALIDATION="1")
        run(linux_build / "fsr3-linux/fsr3_selftest", env=env)
        layer = linux_build / "libVkLayer_xplane_fsr3.so"
    args.output.mkdir(parents=True, exist_ok=True)
    package(args.output, system, layer, probe, sdk, upstream)


if __name__ == "__main__":
    main()
