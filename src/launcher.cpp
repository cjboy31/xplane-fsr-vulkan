// SPDX-License-Identifier: MIT
#include "graphics_settings.hpp"
#include "shared.hpp"
#include <bcrypt.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;
static fs::path appDirectory, simulator;
static HWND window{}, pathBox{}, argsBox{}, statusBox{}, toggle{}, modeBox{}, fgBox{}, ghostBox{},
    cockpitBox{}, tintBox{};
static HANDLE child{}, mapping{}, aaChild{};
static DWORD childId{};
static int launchedFG{}, lastFGSelection{};
static bool checkingFG{}, checkingCockpit{};
static app::Shared *shared{};
static HFONT font{}, titleFont{};
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
        for (auto *name : {L"XPFSR24_COCKPIT_RESTORE", L"XPFSR24_GHOST_REDUCTION", L"VK_LAYER_PATH",
                           L"VK_INSTANCE_LAYERS", L"VK_LOADER_LAYERS_ENABLE", L"VK_LOADER_DEBUG",
                           L"DISABLE_VULKAN_OBS_CAPTURE", L"XPFSR24_FG_CONTROL", L"XPFSR24_MODE",
                           L"XPFSR24_PROVIDER", L"XPFSR24_FG", L"XPFSR24_RUNTIME", L"XPFSR24_LOG",
                           L"XPFSR24_STATUS", L"XPFSR24_CONTROL"}) {
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
            ensure(graphics.msaa == 0 && graphics.fxaa == 0,
                   "Open Graphics setup and turn MSAA and FXAA Off before using temporal FSR");
            ensure((selected == 1 || selected == 3) ? graphics.scale == 0 : graphics.scale > 0,
                   "Open Graphics setup: Native AA needs Full scale; upscaling needs a lower "
                   "rendering scale");
        }
        install(root);
    }
    fs::create_directories(appDirectory / L"logs");
    EnvironmentScope environment;
    auto cockpitRestore = SendMessageW(cockpitBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto alignmentTint = SendMessageW(tintBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ensure(native || !cockpitRestore || SendMessageW(fgBox, CB_GETCURSEL, 0, 0) == 0,
           "Turn frame generation Off for the SR22 cockpit-restore preview, or turn cockpit "
           "restore Off");
    SetEnvironmentVariableW(L"XPFSR24_COCKPIT_RESTORE",
                            !native && cockpitRestore ? (alignmentTint ? L"2" : L"1") : nullptr);
    SetEnvironmentVariableW(L"XPFSR24_GHOST_REDUCTION",
                            SendMessageW(ghostBox, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    auto mode = SendMessageW(modeBox, CB_GETCURSEL, 0, 0);
    auto fg = SendMessageW(fgBox, CB_GETCURSEL, 0, 0);
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
    for (auto *name : {L"renderer.log", L"renderer-status.txt"}) {
        auto file = appDirectory / L"logs" / name;
        if (fs::exists(file))
            fs::copy_file(file, file.wstring() + L".previous",
                          fs::copy_options::overwrite_existing);
        std::ofstream(file, std::ios::trunc);
    }
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
    CloseHandle(process.hThread);
    simulator = root;
    std::ofstream(appDirectory / L"logs/launch.txt")
        << "Launcher PID " << GetCurrentProcessId() << "; X-Plane PID " << childId << '\n';
    WritePrivateProfileStringW(L"Application", L"Simulator", root.c_str(),
                               (appDirectory / L"settings.ini").c_str());
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
                                      : L"Cockpit GPU check passed. Open the cockpit comparison. "
                                        L"Live SR22 alignment still needs inspection.")
            : checkingFG
                ? (result ? L"Frame generation check failed. Read fg-check\\run.txt."
                          : L"Frame generation check passed. See fg-check\\metrics.json for "
                            L"rendered/generated/presentation counts.")
                : (result ? L"Native AA check failed. Open aa-check\\run.txt for details."
                          : L"Native AA check passed. Use View AA comparison for the result."));
    }

    if (!running()) {
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
                }
            }
        }
    }
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
               << L"). Waiting for the camera plugin…\r\nMapping status: " << GetLastError();
        SetWindowTextW(statusBox, detail.str().c_str());
    }
}
static HWND control(const wchar_t *type, const wchar_t *label, DWORD style, int x, int y, int w,
                    int h, int id = 0) {
    auto childWindow =
        CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, label,
                        WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(childWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return childWindow;
}
static void error(const std::exception &e) {
    MessageBoxW(window, wide(e.what()).c_str(), L"X-Plane FSR 2.4 Alpha", MB_OK | MB_ICONERROR);
}
#include "graphics_ui.inc"
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        window = hwnd;
        font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        titleFont = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto title = control(L"STATIC", L"FSR for X-Plane", 0, 28, 22, 650, 40);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        control(L"STATIC", L"SDK 2.4 · Windows Vulkan · Experimental alpha", 0, 30, 66, 650, 28);
        control(L"STATIC", L"X-Plane folder", 0, 30, 112, 650, 24);
        pathBox =
            control(L"EDIT", simulator.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 30, 140, 570, 31, 10);
        control(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, 612, 139, 105, 33, 11);
        control(L"STATIC", L"Optional X-Plane arguments", 0, 30, 187, 650, 24);
        argsBox = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 30, 215, 687, 31, 12);
        control(L"BUTTON", L"Launch selected mode", BS_DEFPUSHBUTTON | WS_TABSTOP, 30, 270, 210, 42,
                20);
        control(L"BUTTON", L"Launch native", BS_PUSHBUTTON | WS_TABSTOP, 254, 270, 174, 42, 21);
        toggle = control(L"BUTTON", L"Enable selected mode", BS_AUTOCHECKBOX | WS_TABSTOP, 450, 276,
                         265, 30, 22);
        SendMessageW(toggle, BM_SETCHECK, BST_CHECKED, 0);
        control(L"STATIC", L"Mode", 0, 30, 330, 70, 24);
        modeBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 100, 325, 617, 160, 40);
        for (auto *name : {L"Diagnostics only (default; native GPU commands)",
                           L"FSR 3.1.6 Native AA - compatibility (experimental)",
                           L"FSR 3.1.6 upscaling - compatibility (experimental)",
                           L"FSR 4.1.2 Native AA - preview", L"FSR 4.1.2 upscaling - preview"})
            SendMessageW(modeBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(modeBox, CB_SETCURSEL, 0, 0);
        control(L"STATIC", L"Frame gen", 0, 30, 373, 125, 24);
        fgBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 165, 368, 552, 130, 41);
        for (auto *label : {L"Off (default)", L"3.1.6 compatibility - experimental 2x",
                            L"4.0.2 ML - experimental 2x preview"})
            SendMessageW(fgBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
        control(L"STATIC",
                L"Start with Native AA 3.1.6, Full resolution, MSAA/FXAA off, FG Off.\r\nUpscaling "
                L"needs below Full. SR22 cockpit restore needs FG Off; use the tint to check "
                L"alignment.",
                0, 30, 417, 685, 48);
        ghostBox =
            control(L"BUTTON", L"Reduce ghosting (SR 3.1.6 only; restart; may increase shimmer)",
                    BS_AUTOCHECKBOX | WS_TABSTOP, 30, 467, 685, 25, 42);
        SendMessageW(ghostBox, BM_SETCHECK, BST_CHECKED, 0);
        cockpitBox = control(L"BUTTON", L"Restore SR22 cockpit screens (Vulkan preview; FG Off)",
                             BS_AUTOCHECKBOX | WS_TABSTOP, 30, 496, 685, 25, 43);
        SendMessageW(cockpitBox, BM_SETCHECK, BST_CHECKED, 0);
        tintBox = control(L"BUTTON", L"Show green screen-alignment tint (restart to apply)",
                          BS_AUTOCHECKBOX | WS_TABSTOP, 30, 525, 685, 25, 44);
        statusBox =
            control(L"EDIT",
                    L"Ready. This alpha uses camera-derived motion.\r\nMoving objects, propellers, "
                    L"glass and cockpit displays need visual validation.\r\nFrame generation is "
                    L"off by default. Gauges/UI can show interpolation artifacts.",
                    ES_MULTILINE | ES_READONLY | WS_VSCROLL, 30, 558, 687, 136, 30);
        control(L"BUTTON", L"Open renderer log", BS_PUSHBUTTON | WS_TABSTOP, 30, 709, 195, 35, 31);
        control(L"BUTTON", L"Remove camera plugin", BS_PUSHBUTTON | WS_TABSTOP, 240, 709, 215, 35,
                32);
        control(L"BUTTON", L"Read guide", BS_PUSHBUTTON | WS_TABSTOP, 473, 709, 150, 35, 33);
        control(L"BUTTON", L"Run Native AA check", BS_PUSHBUTTON | WS_TABSTOP, 30, 761, 215, 35,
                34);
        control(L"BUTTON", L"Graphics setup...", BS_PUSHBUTTON | WS_TABSTOP, 260, 761, 215, 35, 35);
        control(L"BUTTON", L"Run FG check", BS_PUSHBUTTON | WS_TABSTOP, 490, 761, 227, 35, 36);
        control(L"BUTTON", L"Run cockpit check", BS_PUSHBUTTON | WS_TABSTOP, 30, 812, 335, 35, 37);
        control(L"BUTTON", L"View cockpit comparison", BS_PUSHBUTTON | WS_TABSTOP, 385, 812, 332,
                35, 38);
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND:
        try {
            switch (LOWORD(w)) {
            case 11: {
                BROWSEINFOW info{};
                info.hwndOwner = window;
                info.lpszTitle = L"Choose the folder containing X-Plane.exe";
                info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                auto item = SHBrowseForFolderW(&info);
                if (item) {
                    wchar_t selected[MAX_PATH]{};
                    if (SHGetPathFromIDListW(item, selected))
                        SetWindowTextW(pathBox, selected);
                    CoTaskMemFree(item);
                }
                break;
            }
            case 20:
                ensure(!aaChild, "Wait for the Native AA check to finish");
                launch(text(pathBox), text(argsBox), false);
                break;
            case 21:
                launch(text(pathBox), text(argsBox), true);
                break;
            case 22: {
                bool enabled = SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (shared)
                    InterlockedExchange(&shared->enabled, enabled);
                std::ofstream(appDirectory / L"logs/renderer-control.txt") << (enabled ? '1' : '0');
                break;
            }
            case 31:
                ShellExecuteW(hwnd, L"open", L"notepad.exe",
                              quote((appDirectory / L"logs/renderer.log").wstring()).c_str(),
                              nullptr, SW_SHOWNORMAL);
                break;
            case 32:
                removePlugin(text(pathBox));
                SetWindowTextW(statusBox, L"This package's camera plugin has been removed.");
                break;
            case 33: {
                auto guide = appDirectory / L"INSTALLATION-AND-USE.txt";
                if (!fs::exists(guide))
                    guide = appDirectory / L"README.txt";
                ShellExecuteW(hwnd, L"open", L"notepad.exe", quote(guide.wstring()).c_str(),
                              nullptr, SW_SHOWNORMAL);
                break;
            }
            case 41:
                if (HIWORD(w) == CBN_SELCHANGE && running()) {
                    int selected = int(SendMessageW(fgBox, CB_GETCURSEL, 0, 0));
                    if (selected && selected != launchedFG) {
                        SendMessageW(fgBox, CB_SETCURSEL, lastFGSelection, 0);
                        throw std::runtime_error(
                            "Restart X-Plane to change the frame generation provider");
                    }
                    std::ofstream(appDirectory / L"logs/renderer-fg-control.txt")
                        << (selected ? '1' : '0');
                    lastFGSelection = selected;
                }
                break;
            case 36:
                checkFG();
                break;
            case 37:
                checkCockpit();
                break;
            case 38:
                ShellExecuteW(hwnd, L"open",
                              (appDirectory / L"cockpit-check/comparison.html").c_str(), nullptr,
                              nullptr, SW_SHOWNORMAL);
                break;
            case 34:
                checkAA();
                break;
            case 35:
                showGraphics();
                break;
            }
            refresh();
        } catch (const std::exception &e) {
            error(e);
        }
        return 0;
    case WM_TIMER:
        refresh();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        detach();
        if (child)
            CloseHandle(child);
        if (aaChild)
            CloseHandle(aaChild);
        if (font)
            DeleteObject(font);
        if (titleFont)
            DeleteObject(titleFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    appDirectory = fs::path(self).parent_path();
    wchar_t saved[32768]{};
    GetPrivateProfileStringW(L"Application", L"Simulator", L"D:\\X-Plane 12", saved, 32768,
                             (appDirectory / L"settings.ini").c_str());
    simulator = saved;
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
        auto existing = FindWindowW(L"XPFSR24Launcher", nullptr);
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
    c.lpszClassName = L"XPFSR24Launcher";
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&c);
    auto hwnd =
        CreateWindowExW(0, c.lpszClassName, L"X-Plane FSR - SR22 Vulkan Cockpit Preview",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                        CW_USEDEFAULT, 765, 925, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CloseHandle(applicationLock);
    CoUninitialize();
    return int(message.wParam);
}
