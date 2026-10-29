// SPDX-License-Identifier: MIT
#include "graphics_settings.hpp"
#include "shared.hpp"
#include "launcher_preferences.hpp"
#include "runtime_status.hpp"
#include "session_logs.hpp"
#include "launcher_theme.hpp"
#include "framegen_policy.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;
static fs::path appDirectory, simulator;
static std::wstring launcherWindowClass;
static HWND window{}, pathBox{}, argsBox{}, statusBox{}, toggle{}, modeBox{}, fgBox{}, ghostBox{},
    cockpitBox{}, tintBox{};
static HWND presetBox{}, presetNote{}, savedLabel{}, injectionBanner{}, motionSetupNote{},
    footerStatus{};
static const wchar_t *designSetupHint();
static launcher::Preferences preferences;
static launcher::RuntimeMonitor runtimeMonitor;
static launcher::RuntimeState bannerState{};
static bool controlsReady{}, launchedNative{}, activeNotificationSent{};
static void saveChoices();
static void runtimeBanner(launcher::RuntimeState, const app::Telemetry &);
static HANDLE child{}, mapping{}, aaChild{};
static DWORD childId{};
static int launchedFG{}, lastFGSelection{};
static bool checkingFG{}, checkingCockpit{};
static app::Shared *shared{};
static HFONT font{}, titleFont{};
static int preferredWindowSize(const wchar_t *key, int fallback, int minimum, int maximum) {
    try {
        return std::clamp(std::stoi(launcher::readSetting(appDirectory / L"settings.ini", L"Window",
                                                          key, std::to_wstring(fallback).c_str())),
                          minimum, maximum);
    } catch (...) {
        return fallback;
    }
}
static void saveWindowSize(HWND h) {
    if (IsIconic(h) || IsZoomed(h))
        return;
    RECT r;
    GetClientRect(h, &r);
    auto file = appDirectory / L"settings.ini";
    int scroll = (GetWindowLongPtrW(h, GWL_STYLE) & WS_VSCROLL) ? theme::scrollbarWidth() : 0;
    WritePrivateProfileStringW(L"Window", L"ClientWidth",
                               std::to_wstring(MulDiv(r.right + scroll, 96, theme::dpi)).c_str(),
                               file.c_str());
    WritePrivateProfileStringW(L"Window", L"ClientHeight",
                               std::to_wstring(MulDiv(r.bottom, 96, theme::dpi)).c_str(),
                               file.c_str());
}
static std::wstring text(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring value(n + 1, L'\0');
    GetWindowTextW(control, value.data(), n + 1);
    value.resize(n);
    return value;
}
static std::wstring wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, result.data(), n);
    if (n)
        result.pop_back();
    return result;
}
static void ensure(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
static bool running() { return child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT; }
static std::string hash(const fs::path &file) {
    std::ifstream stream(file, std::ios::binary);
    ensure(bool(stream), "A required file is missing");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE object{};
    ensure(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
           "SHA256 provider failed");
    DWORD objectBytes{}, used{};
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                      sizeof(objectBytes), &used, 0);
    std::vector<UCHAR> storage(objectBytes);
    BCryptCreateHash(algorithm, &object, storage.data(), objectBytes, nullptr, 0, 0);
    char buffer[65536];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        auto count = stream.gcount();
        if (count)
            BCryptHashData(object, reinterpret_cast<PUCHAR>(buffer), ULONG(count), 0);
    }
    UCHAR digest[32]{};
    BCryptFinishHash(object, digest, 32, 0);
    BCryptDestroyHash(object);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::ostringstream result;
    for (auto v : digest)
        result << std::hex << std::setw(2) << std::setfill('0') << unsigned(v);
    return result.str();
}
static void preflight(const fs::path &root, bool native = false) {
    ensure(fs::exists(root / L"X-Plane.exe"), "Choose the folder containing X-Plane.exe");
    if (native)
        return;
    ensure(fs::exists(appDirectory / L"layer/XPFSR24Layer.dll") &&
               fs::exists(appDirectory / L"layer/XPFSR24Layer.json"),
           "The FSR layer is missing; extract the complete application folder");
    ensure(fs::exists(appDirectory / L"plugin/64/win.xpl"), "The camera plugin is missing");
    ensure(hash(appDirectory / L"runtime/amd_fidelityfx_loader_vk.dll") ==
               "38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9",
           "AMD loader is missing or differs from the verified SDK 2.4 package");
    ensure(hash(appDirectory / L"runtime/amd_fidelityfx_upscaler_vk.dll") ==
               "4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7",
           "AMD upscaler is missing or differs from the verified SDK 2.4 package");
    if (app::frameGenerationAvailable)
        ensure(hash(appDirectory / L"runtime/amd_fidelityfx_framegeneration_vk.dll") ==
                   "d002dabff8c5e71eaa715fc99438cd4dc81230262a7897b16bc402efec3498b4",
               "AMD frame generation DLL is missing or differs from SDK 2.4");
    ensure(!fs::exists(root / L"vulkan-1.dll"), "A local vulkan-1.dll proxy is installed. Disable "
                                                "the other rendering mod before testing FSR Alpha");
    for (auto *name : {L"LuminaXP", L"MotionVectors"})
        ensure(!fs::exists(root / L"Resources/plugins" / name),
               "LuminaXP or MotionVectors is installed. Move the other renderer plugin out before "
               "this isolated test");
}
static void install(const fs::path &root) {
    ensure(!running(),
           "Close the X-Plane process launched here before installing the camera plugin");
    preflight(root);
    auto target = root / L"Resources/plugins/XPFSR24/64/win.xpl";
    if (fs::exists(target)) {
        auto old = hash(target), current = hash(appDirectory / L"plugin/64/win.xpl");
        if (old == current)
            return;
        ensure(old == "bf790e7915cd7afd125daf297eac12d36557f69c0a29d66dbb396522995826a0" ||
                   old == "d799da598058143fc99f63180da8a66d4cbe078bd6f105fd66a4a38b81a1d888" ||
                   old == "5fb87828f94aca906601703d49f0626aeac85e38ebf7b068b00867cd18535720" ||
                   old == "65cc058436aefcc810530b441cf29b6ee3093b2230a7b5b543f5a2b2a3eb1e01" ||
                   old == "370ee0ff5119aba274755d7f0ed67825b338df35b13c8665db8ba4af229e5b96" ||
                   old == "6b0b17f2d315b5d50018dd167e896cd54a814ad11aa03ad6d94d2a14ed882e3a" ||
                   old == "119db0cae84dc5873c31eb42bb3afddd90c4a34c1fa2a949a1c056d394dc1045",
               "Installed XPFSR24 plugin is not a recognized owned build; no file was replaced");
        auto backups = appDirectory / L"plugin-backups";
        fs::create_directories(backups);
        auto backup = backups / (L"camera-previous-" + wide(old.c_str()) + L".xpl");
        if (fs::exists(backup))
            ensure(hash(backup) == old, "Camera backup differs from the recognized build");
        else
            fs::copy_file(target, backup);
        fs::copy_file(appDirectory / L"plugin/64/win.xpl", target,
                      fs::copy_options::overwrite_existing);
        return;
    }
    fs::create_directories(target.parent_path());
    fs::copy_file(appDirectory / L"plugin/64/win.xpl", target,
                  fs::copy_options::overwrite_existing);
}
static void removePlugin(const fs::path &root) {
    ensure(!running(), "Close the X-Plane process launched here before removing the camera plugin");
    auto directory = root / L"Resources/plugins/XPFSR24", target = directory / L"64/win.xpl";
    if (!fs::exists(target))
        return;
    ensure(hash(target) == hash(appDirectory / L"plugin/64/win.xpl"),
           "Installed plugin differs from this package; no file was removed");
    fs::remove(target);
    if (fs::is_empty(directory / L"64"))
        fs::remove(directory / L"64");
    if (fs::is_empty(directory))
        fs::remove(directory);
}
static std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += c;
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
static void detach() {
    if (shared)
        UnmapViewOfFile(shared);
    if (mapping)
        CloseHandle(mapping);
    shared = nullptr;
    mapping = nullptr;
}
struct EnvironmentScope {
    std::vector<std::pair<std::wstring, std::wstring>> values;
    EnvironmentScope() {
        for (auto *name :
             {L"XPFSR24_COCKPIT_RESTORE", L"XPFSR24_GHOST_REDUCTION", L"VK_LAYER_PATH",
              L"VK_INSTANCE_LAYERS", L"VK_LOADER_LAYERS_ENABLE", L"VK_LOADER_DEBUG",
              L"DISABLE_VULKAN_OBS_CAPTURE", L"XPFSR24_FG_CONTROL", L"XPFSR24_MODE",
              L"XPFSR24_SESSION", L"XPFSR24_PROVIDER", L"XPFSR24_FG", L"XPFSR24_RUNTIME",
              L"XPFSR24_LOG", L"XPFSR24_STATUS", L"XPFSR24_CONTROL"}) {
            wchar_t value[32768]{};
            GetEnvironmentVariableW(name, value, 32768);
            values.emplace_back(name, value);
        }
    }
    ~EnvironmentScope() {
        for (auto &item : values)
            SetEnvironmentVariableW(item.first.c_str(),
                                    item.second.empty() ? nullptr : item.second.c_str());
    }
};
static void launch(const fs::path &root, const std::wstring &arguments, bool native) {
    ensure(!running(), "X-Plane is already running from this launcher");
    preflight(root, native);
    if (!native) {
        graphics::ensureSimulatorClosed();
        auto graphics = graphics::current(root);
        auto selected = SendMessageW(modeBox, CB_GETCURSEL, 0, 0);
        if (selected > 0) {
            ensure(graphics.fxaa == 0,
                   "FXAA is supported for native comparison only. Turn FXAA Off on the main screen "
                   "and Apply graphics; your FSR and frame gen choices will be retained.");
            ensure((selected == 1 || selected == 3) ? graphics.scale == 0 : graphics.scale > 0,
                   "Choose a matching preset: Native AA needs Full scale; upscaling needs a lower "
                   "rendering scale");
        }
        install(root);
    }
    fs::create_directories(appDirectory / L"logs");
    EnvironmentScope environment;
    auto cockpitRestore = SendMessageW(cockpitBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto alignmentTint = SendMessageW(tintBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ensure(native || !cockpitRestore || SendMessageW(fgBox, CB_GETCURSEL, 0, 0) == 0,
           "Turn frame generation Off for FSR Ghosting Fix cockpit screens, or turn that fix Off");
    SetEnvironmentVariableW(L"XPFSR24_COCKPIT_RESTORE",
                            !native && cockpitRestore ? (alignmentTint ? L"2" : L"1") : nullptr);
    SetEnvironmentVariableW(L"XPFSR24_GHOST_REDUCTION",
                            SendMessageW(ghostBox, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    auto mode = SendMessageW(modeBox, CB_GETCURSEL, 0, 0);
    auto fg = app::effectiveFrameGeneration(int(SendMessageW(fgBox, CB_GETCURSEL, 0, 0)));
    ensure(native || !fg || mode > 0,
           "Select Native AA or upscaling before enabling frame generation");
    if (!native && fg)
        SetEnvironmentVariableW(L"DISABLE_VULKAN_OBS_CAPTURE", L"1");
    launchedFG = native ? 0 : int(fg);
    lastFGSelection = int(fg);
    SetEnvironmentVariableW(L"XPFSR24_FG_CONTROL",
                            (appDirectory / L"logs/renderer-fg-control.txt").c_str());
    SetEnvironmentVariableW(L"XPFSR24_FG", !native && fg == 1   ? L"3.1.6"
                                           : !native && fg == 2 ? L"4.0.2"
                                                                : nullptr);
    SetEnvironmentVariableW(L"XPFSR24_MODE", (mode == 1 || mode == 3)   ? L"native-aa"
                                             : (mode == 2 || mode == 4) ? L"upscale"
                                                                        : L"observe");
    SetEnvironmentVariableW(L"XPFSR24_PROVIDER", mode >= 3 ? L"4.1.2" : L"3.1.6");
    std::ostringstream sessionChoices;
    sessionChoices << "requested_mode=" << mode << " provider=" << (mode >= 3 ? "4.1.2" : "3.1.6")
                   << " frame_generation=" << fg << " native_comparison=" << native;
    auto sessionId = session::prepare(appDirectory / L"logs", sessionChoices.str());
    SetEnvironmentVariableW(L"XPFSR24_SESSION", wide(sessionId.c_str()).c_str());
    SetEnvironmentVariableW(L"VK_LAYER_PATH", native ? nullptr : (appDirectory / L"layer").c_str());
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", native ? nullptr : L"VK_LAYER_XPFSR24");
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_ENABLE", native ? nullptr : L"VK_LAYER_XPFSR24");
    SetEnvironmentVariableW(L"XPFSR24_RUNTIME", (appDirectory / L"runtime").c_str());
    SetEnvironmentVariableW(L"XPFSR24_LOG", (appDirectory / L"logs/renderer.log").c_str());
    SetEnvironmentVariableW(L"XPFSR24_STATUS",
                            (appDirectory / L"logs/renderer-status.txt").c_str());
    SetEnvironmentVariableW(L"XPFSR24_CONTROL",
                            (appDirectory / L"logs/renderer-control.txt").c_str());
    std::ofstream(appDirectory / L"logs/renderer-fg-control.txt") << (fg ? '1' : '0');
    std::ofstream(appDirectory / L"logs/renderer-control.txt")
        << (SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED ? '1' : '0');
    SetEnvironmentVariableW(L"VK_LOADER_DEBUG", nullptr);
    auto executable = root / L"X-Plane.exe";
    std::wstring command = quote(executable.wstring()) + L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    ensure(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          root.c_str(), &startup, &process) != FALSE,
           "Windows could not launch X-Plane");
    if (child)
        CloseHandle(child);
    detach();
    child = process.hProcess;
    childId = process.dwProcessId;
    launchedNative = native;
    runtimeMonitor.reset();
    activeNotificationSent = false;
    runtimeBanner(native ? launcher::RuntimeState::native : launcher::RuntimeState::starting, {});
    CloseHandle(process.hThread);
    simulator = root;
    std::ofstream(appDirectory / L"logs/launch.txt", std::ios::app)
        << "Launcher PID " << GetCurrentProcessId() << "; X-Plane PID " << childId << '\n';
}
static void checkAA() {
    checkingFG = false;
    checkingCockpit = false;
    ensure(!running(), "Close the simulator before running the isolated Native AA check");
    ensure(!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT,
           "The Native AA check is already running");
    if (aaChild) {
        CloseHandle(aaChild);
        aaChild = nullptr;
    }
    fs::create_directories(appDirectory / L"aa-check");
    auto executable = appDirectory / L"NativeAACheck.exe";
    auto command = quote(executable.wstring()) + L" " +
                   quote((appDirectory / L"runtime").wstring()) + L" " +
                   quote((appDirectory / L"aa-check").wstring()) + L" " +
                   (SendMessageW(modeBox, CB_GETCURSEL, 0, 0) >= 3 ? L"4.1.2" : L"3.1.6");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE output =
        CreateFileW((appDirectory / L"aa-check/run.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(output != INVALID_HANDLE_VALUE, "Cannot create AA check log");
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    bool started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, appDirectory.c_str(), &startup, &process) != FALSE;
    CloseHandle(output);
    ensure(started, "Could not start NativeAACheck.exe");
    CloseHandle(process.hThread);
    aaChild = process.hProcess;
    SetWindowTextW(statusBox,
                   L"Running the isolated Native AA check: 48 small, fenced GPU frames...");
}
static void checkFG() {
    ensure(app::frameGenerationAvailable,
           "Built-in frame generation is suspended in regular releases");
    checkingCockpit = false;
    checkingFG = true;
    ensure(!running(), "Close the simulator before running the isolated Native AA check");
    ensure(!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT,
           "The Native AA check is already running");
    if (aaChild) {
        CloseHandle(aaChild);
        aaChild = nullptr;
    }
    fs::create_directories(appDirectory / L"fg-check");
    auto executable = appDirectory / L"FrameGenCheck.exe";
    auto command = quote(executable.wstring()) + L" " +
                   quote((appDirectory / L"runtime").wstring()) + L" " +
                   quote((appDirectory / L"fg-check").wstring()) + L" " +
                   (SendMessageW(fgBox, CB_GETCURSEL, 0, 0) == 2 ? L"4.0.2" : L"3.1.6");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE output =
        CreateFileW((appDirectory / L"fg-check/run.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(output != INVALID_HANDLE_VALUE, "Cannot create AA check log");
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    bool started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, appDirectory.c_str(), &startup, &process) != FALSE;
    CloseHandle(output);
    ensure(started, "Could not start FrameGenCheck.exe");
    CloseHandle(process.hThread);
    aaChild = process.hProcess;
    SetWindowTextW(statusBox,
                   L"Running frame generation check: 90 rendered frames in a temporary window...");
}
static void checkCockpit() {
    ensure(!running(), "Close X-Plane before running the isolated cockpit check");
    ensure(!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT,
           "A GPU check is already running");
    if (aaChild) {
        CloseHandle(aaChild);
        aaChild = nullptr;
    }
    checkingCockpit = true;
    checkingFG = false;
    auto directory = appDirectory / L"cockpit-check";
    fs::create_directories(directory);
    auto executable = appDirectory / L"CockpitRestoreCheck.exe";
    std::wstring command = quote(executable.wstring()) + L" " +
                           quote((appDirectory / L"runtime").wstring()) + L" " +
                           (SendMessageW(modeBox, CB_GETCURSEL, 0, 0) >= 3 ? L"4.1.2" : L"3.1.6") +
                           L" " + quote(directory.wstring());
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE output = CreateFileW((directory / L"run.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(output != INVALID_HANDLE_VALUE, "Cannot create cockpit check log");
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output;
    PROCESS_INFORMATION process{};
    bool started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, appDirectory.c_str(), &startup, &process) != FALSE;
    CloseHandle(output);
    ensure(started, "Could not start CockpitRestoreCheck.exe");
    CloseHandle(process.hThread);
    aaChild = process.hProcess;
    SetWindowTextW(
        statusBox,
        L"Running isolated Vulkan cockpit compositor check; no simulator files are changed.");
}
static void refresh() {
    if (aaChild && WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT) {
        DWORD result{};
        GetExitCodeProcess(aaChild, &result);
        CloseHandle(aaChild);
        aaChild = nullptr;
        SetWindowTextW(
            statusBox,
            checkingCockpit ? (result ? L"Cockpit GPU check failed. Read cockpit-check/run.txt."
                                      : L"Ghosting-fix GPU check passed. Open the comparison. "
                                        L"Live screen alignment still needs inspection.")
            : checkingFG
                ? (result ? L"Frame generation check failed. Read fg-check\\run.txt."
                          : L"Frame generation check passed. See fg-check\\metrics.json for "
                            L"rendered/generated/presentation counts.")
                : (result ? L"Native AA check failed. Open aa-check\\run.txt for details."
                          : L"Native AA check passed. Use View AA comparison for the result."));
    }

    bool activeProcess = running();
    for (int id : {10, 11, 12, 20, 21, 35, 40, 42, 43, 44, 107, 109})
        EnableWindow(GetDlgItem(window, id), !activeProcess);
    if (!activeProcess) {
        runtimeBanner(launcher::RuntimeState::stopped, {});
        if (child) {
            DWORD exitCode{};
            GetExitCodeProcess(child, &exitCode);
            std::ofstream(appDirectory / L"logs/exit-status.txt")
                << "X-Plane exit code: " << exitCode << "\n";
            auto message =
                exitCode ? L"X-Plane exited with Windows code " + std::to_wstring(exitCode) +
                               L". Read the renderer log."
                         : std::wstring(
                               L"X-Plane closed normally. The renderer log is available below.");
            SetWindowTextW(statusBox, message.c_str());
            CloseHandle(child);
            child = nullptr;
            detach();
        }
        return;
    }
    if (!shared) {
        auto key = app::mappingName(childId);
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, key.c_str());
        if (mapping)
            shared = static_cast<app::Shared *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(app::Shared)));
    }
    app::Telemetry status{};
    bool available = app::readStatus(shared, status);
    if (!available) {
        std::ifstream f(appDirectory / L"logs/renderer-status.txt");
        std::string reason, provider;
        if (std::getline(f, reason) && std::getline(f, provider)) {
            strncpy(status.reason, reason.c_str(), sizeof(status.reason) - 1);
            strncpy(status.provider, provider.c_str(), sizeof(status.provider) - 1);
            available = bool(f >> status.presents >> status.recordings >> status.bypasses >>
                             status.renderWidth >> status.renderHeight >> status.outputWidth >>
                             status.outputHeight >> status.active);
            if (available) {
                std::string ignored;
                std::getline(f, ignored);
                std::string fgProvider;
                if (std::getline(f, fgProvider)) {
                    strncpy(status.fgProvider, fgProvider.c_str(), 63);
                    f >> status.fgPrepared >> status.fgGenerated >> status.displayedPresents >>
                        status.fgActive;
                    std::getline(f, ignored);
                    std::string fgReason;
                    std::getline(f, fgReason);
                    strncpy(status.fgReason, fgReason.c_str(), 159);
                    f >> status.cockpitRecordings >> status.cockpitActive >> status.cockpitScreens;
                    std::getline(f, ignored);
                    std::string cockpitReason;
                    std::getline(f, cockpitReason);
                    strncpy(status.cockpitReason, cockpitReason.c_str(), 159);
                }
            }
        }
    }
    auto state = runtimeMonitor.update(true, launchedNative, available, status,
                                       SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED,
                                       GetTickCount64());
    runtimeBanner(state, status);
    if (available) {
        std::wostringstream line;
        line << (status.active ? L"FSR reconstruction recorded" : L"Native rendering / waiting")
             << L"\r\n"
             << wide(status.reason) << L"\r\n";
        if (status.provider[0])
            line << L"SR " << wide(status.provider) << L"   " << status.renderWidth << L" x "
                 << status.renderHeight << L" -> " << status.outputWidth << L" x "
                 << status.outputHeight << L"\r\n";
        line << L"Rendered presents: " << status.presents << L"   FSR recordings: "
             << status.recordings << L"\r\n";
        if (status.fgProvider[0] || status.fgReason[0])
            line << L"FG " << wide(status.fgProvider) << L"  "
                 << (status.fgActive ? L"Active" : L"Off / waiting") << L"\r\nGenerated outputs: "
                 << status.fgGenerated << L"   SDK presents: " << status.displayedPresents
                 << L"\r\n"
                 << wide(status.fgReason) << L"\r\n";
        line << L"Cockpit: " << wide(status.cockpitReason) << L"\r\nRestore recordings: "
             << status.cockpitRecordings << L"\r\n";
        line << L"Counters are not measured FPS or a visual-quality guarantee.";
        SetWindowTextW(statusBox, line.str().c_str());
        if (shared)
            SendMessageW(toggle, BM_SETCHECK,
                         InterlockedCompareExchange(&shared->enabled, 0, 0) ? BST_CHECKED
                                                                            : BST_UNCHECKED,
                         0);
    } else {
        std::wostringstream detail;
        detail << L"X-Plane is running (PID " << childId
               << (launchedNative ? L"). Native launch; FSR injection was not requested."
                                  : L"). Waiting for renderer telemetry. Load a flight.\r\nA "
                                    L"running simulator alone does not confirm injection.");
        SetWindowTextW(statusBox, detail.str().c_str());
    }
}
#include "launcher_ui.inc"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    theme::enableDpi();
    theme::dpi = theme::systemDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    appDirectory = fs::path(self).parent_path();
    launcherWindowClass =
        L"XPFSR24Launcher_" + std::to_wstring(std::hash<std::wstring>{}(appDirectory.wstring()));
    preferences = launcher::load(appDirectory / L"settings.ini");
    simulator = preferences.simulator;
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    try {
        for (int i = 1; i < argc; ++i)
            if (std::wstring(argv[i]) == L"--preflight") {
                if (i + 1 < argc)
                    simulator = argv[i + 1];
                preflight(simulator);
                fs::create_directories(appDirectory / L"logs");
                std::ofstream(appDirectory / L"logs/preflight.txt")
                    << "PASS: simulator, camera plugin, layer and verified AMD runtime package "
                       "present.\n";
                LocalFree(argv);
                CoUninitialize();
                return 0;
            }
    } catch (const std::exception &e) {
        fs::create_directories(appDirectory / L"logs");
        std::ofstream(appDirectory / L"logs/preflight.txt") << "FAIL: " << e.what() << '\n';
        LocalFree(argv);
        CoUninitialize();
        return 1;
    }
    LocalFree(argv);
    HANDLE applicationLock =
        CreateFileW((appDirectory / L"launcher.lock").c_str(), GENERIC_WRITE, 0, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (applicationLock == INVALID_HANDLE_VALUE) {
        auto existing = FindWindowW(launcherWindowClass.c_str(), nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        } else
            MessageBoxW(nullptr,
                        L"Extract the complete application into a writable folder, or use the "
                        L"already open launcher.",
                        L"FSR for X-Plane", MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW c{};
    c.lpfnWndProc = procedure;
    c.hInstance = instance;
    c.lpszClassName = launcherWindowClass.c_str();
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&c);
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT size{0, 0, theme::px(preferredWindowSize(L"ClientWidth", 1260, 840, 1800)),
              theme::px(preferredWindowSize(L"ClientHeight", 1040, 600, 1800))};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    int width = std::min(int(size.right - size.left), int(work.right - work.left) - theme::px(24));
    int height = std::min(int(size.bottom - size.top), int(work.bottom - work.top) - theme::px(24));
    auto hwnd = CreateWindowExW(0, c.lpszClassName, L"FSR 3/4 + Frame Gen for X-Plane", style,
                                work.left + (work.right - work.left - width) / 2,
                                work.top + (work.bottom - work.top - height) / 2, width, height,
                                nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        auto active = GetActiveWindow();
        if (!active || !IsDialogMessageW(active, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CloseHandle(applicationLock);
    CoUninitialize();
    return int(message.wParam);
}
