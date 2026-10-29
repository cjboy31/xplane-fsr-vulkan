"""Native preview configuration, launch environment and owned probe installation.

SPDX-License-Identifier: MIT
Only the explicitly launched child receives Vulkan environment changes.
"""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import uuid

VERSION = "0.6.2-platform-preview.1"
LAYER = "VK_LAYER_XPLANE_fsr3"
MODES = {"FSR 3.1 upscaling": "replace", "Compare: X-Plane FSR 1": "off",
         "Diagnostic: observe only": "observe"}
DEFAULTS = {"schema": 1, "sim": "", "mode": "FSR 3.1 upscaling", "arguments": ""}


def user_root(system=None, env=None):
    system, env = system or platform.system(), os.environ if env is None else env
    home = Path(env.get("HOME", str(Path.home())))
    if system == "Darwin":
        return home / "Library/Application Support/XPlaneFSRNative"
    return Path(env.get("XDG_CONFIG_HOME", home / ".config")) / "xplane-fsr-native"


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".xpfsr-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            json.dump(value, out, indent=2, allow_nan=False)
            out.write("\n")
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_settings(root):
    path = Path(root) / "settings.json"
    result = dict(DEFAULTS)
    if path.exists():
        # Never silently overwrite unreadable settings with defaults.
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or data.get("schema", 1) != 1:
            raise ValueError("Unknown settings format; retain settings.json and report this error.")
        result.update({k: data[k] for k in DEFAULTS if isinstance(data.get(k), type(DEFAULTS[k]))})
    if result["mode"] not in MODES:
        raise ValueError("Unknown saved mode. Retain settings.json and report this error.")
    return result


def save_settings(root, values):
    if values["mode"] not in MODES:
        raise ValueError("Unsupported mode")
    atomic_json(Path(root) / "settings.json", {k: values[k] for k in DEFAULTS})


def simulator_executable(folder, system):
    folder = Path(folder).expanduser().resolve()
    if system == "Linux":
        executable = folder / "X-Plane-x86_64"
    elif system == "Darwin":
        executable = folder / "X-Plane.app/Contents/MacOS/X-Plane"
    else:
        raise ValueError("This preview targets native Linux and macOS only.")
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError("Choose the X-Plane 12 folder containing its native executable.")
    return executable


def launch_environment(package, session, mode, inherited=None, system="Linux"):
    env = dict(os.environ if inherited is None else inherited)
    if mode not in MODES:
        raise ValueError("Unsupported mode")
    # An inherited Windows or experimental FG setting must not enable FG here.
    for key in tuple(env):
        if key.startswith("XPFSR24_") or key.startswith("XP_FSR3"):
            del env[key]
    inherited_layers = [x for x in env.get("VK_INSTANCE_LAYERS", "").split(":") if x and x != LAYER]
    if inherited_layers:
        env["VK_INSTANCE_LAYERS"] = ":".join(inherited_layers)
    else:
        env.pop("VK_INSTANCE_LAYERS", None)
    env["XPFSR24_FRAMEGEN"] = "0"
    env["XPFSR_NATIVE_REPORT"] = str(Path(session) / "compatibility.json")
    if system != "Linux" or MODES[mode] == "off":
        return env
    package = Path(package).resolve()
    if not (package / "libVkLayer_xplane_fsr3.so").is_file():
        raise ValueError("Linux Vulkan backend is missing. Extract the complete Linux package.")
    env["XP_FSR3"] = MODES[mode]
    env["XP_FSR3_OUTPUT"] = str(Path(session).resolve())
    for key in ("VK_ADD_LAYER_PATH", "VK_LAYER_PATH"):
        # VK_LAYER_PATH is also set for older loaders; keep standard and user layers visible.
        standard = ["/usr/local/share/vulkan/explicit_layer.d", "/usr/share/vulkan/explicit_layer.d",
                    "/etc/vulkan/explicit_layer.d",
                    str(Path(env.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "vulkan/explicit_layer.d")]
        values = [str(package)] + ([env[key]] if env.get(key) else (standard if key == "VK_LAYER_PATH" else []))
        env[key] = ":".join(values)
    env["VK_INSTANCE_LAYERS"] = ":".join([LAYER] + inherited_layers)
    return env


def create_session(root):
    session = Path(root) / "sessions" / uuid.uuid4().hex
    session.mkdir(parents=True, exist_ok=False)
    return session


def renderer_status(session, running, system):
    if not running:
        return "SESSION ENDED — launch again for a fresh renderer status."
    if system == "Darwin":
        report = Path(session) / "compatibility.json"
        return ("PROBE LOADED — Metal upscaling is not implemented." if report.exists()
                else "MAC DIAGNOSTICS — waiting for the optional probe; no FSR injection.")
    status = Path(session) / "status.txt"
    if not status.exists():
        return "STARTING — renderer has not confirmed injection. Load a flight and enable FSR supersampling."
    # Freshness matters: a stopped/stalled renderer must not retain an ACTIVE banner.
    import time
    if time.time() - status.stat().st_mtime > 30:
        return "NO RECENT RENDERER CONFIRMATION — last status is over 30 seconds old."
    return status.read_text(encoding="utf-8", errors="replace")[:6000]


def install_probe(package, sim, system):
    simulator_executable(sim, system)
    name = "mac.xpl" if system == "Darwin" else "lin.xpl"
    source = Path(package) / "probe/64" / name
    if not source.is_file():
        raise ValueError("Probe binary is missing from this package.")
    target = Path(sim).resolve() / "Resources/plugins/XPFSRNativeProbe"
    marker = target / "xpfsr-native-owned.json"
    if target.is_symlink() or (target.exists() and not marker.is_file()):
        raise ValueError("An unrecognized probe folder already exists; it was left untouched.")
    if target.exists():
        data = json.loads(marker.read_text())
        if data.get("owner") != "xplane-fsr-native-probe":
            raise ValueError("Unrecognized probe ownership; no files were changed.")
        # Updating must not follow links into another plugin or user folder.
        if (target / "64").is_symlink() or (target / "64" / name).is_symlink():
            raise ValueError("Probe contains symbolic links; no files were changed.")
    (target / "64").mkdir(parents=True, exist_ok=True)
    # Stage then replace so a failed copy does not truncate an existing plugin.
    fd, staged = tempfile.mkstemp(prefix=".probe-", dir=target / "64")
    try:
        with os.fdopen(fd, "wb") as out, source.open("rb") as src:
            shutil.copyfileobj(src, out)
        os.chmod(staged, 0o755)
        os.replace(staged, target / "64" / name)
    finally:
        if os.path.exists(staged):
            os.unlink(staged)
    atomic_json(marker, {"owner": "xplane-fsr-native-probe", "version": VERSION, "binary": name})
    return target


def uninstall_probe(sim):
    target = Path(sim).resolve() / "Resources/plugins/XPFSRNativeProbe"
    marker = target / "xpfsr-native-owned.json"
    if target.is_symlink() or not marker.is_file():
        raise ValueError("Owned probe installation not found; nothing removed.")
    data = json.loads(marker.read_text())
    if data.get("owner") != "xplane-fsr-native-probe" or data.get("binary") not in ("mac.xpl", "lin.xpl"):
        raise ValueError("Unrecognized probe ownership; nothing removed.")
    binary = target / "64" / data["binary"]
    if (target / "64").is_symlink() or binary.is_symlink():
        raise ValueError("Probe contains symbolic links; nothing removed.")
    binary.unlink(missing_ok=True)
    marker.unlink()
    # Empty directories only: never delete unrelated files or user settings.
    for folder in (target / "64", target):
        try:
            folder.rmdir()
        except OSError:
            pass


def launch(package, root, settings, arguments, system=None):
    system = system or platform.system()
    exe = simulator_executable(settings["sim"], system)
    session = create_session(root)
    env = launch_environment(package, session, settings["mode"], system=system)
    # list argv, no shell; joystick, camera and simulator preference files are not written.
    process = subprocess.Popen([str(exe), *arguments], cwd=exe.parents[3] if system == "Darwin"
                               else exe.parent, env=env)
    atomic_json(session / "session.json", {"version": VERSION, "pid": process.pid,
                "platform": system, "mode": settings["mode"], "frame_generation": False})
    return process, session
