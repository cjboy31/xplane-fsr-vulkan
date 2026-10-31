// SPDX-License-Identifier: MIT
// Offline, versioned onboarding. Media Foundation decodes the bundled MP4;
// there is no browser, remote script, codec download, telemetry or simulator hook.
#include "intro_policy.hpp"
#include <mfapi.h>
#include <mfmediaengine.h>
#include <atomic>
namespace intro {
static constexpr UINT eventMessage = WM_APP + 51, startMessage = WM_APP + 52;
static HWND dialog{}, owner{}, video{}, skip{}, pause{}, mute{}, remember{}, progress{}, label{};
static IMFMediaEngine *engine{};
static IMFMediaEngineEx *extended{};
static bool mfStarted{}, loaded{}, playbackFailed{};
static HMODULE mediaFoundation{};
static decltype(&MFStartup) startup{};
static decltype(&MFShutdown) shutdown{};
static decltype(&MFCreateAttributes) createAttributes{};
static ULONGLONG startTick{};
static fs::path mediaPath;
class Notify final : public IMFMediaEngineNotify {
    LONG references{1};
  public:
    std::atomic<HWND> target{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFMediaEngineNotify)) {
            *out = static_cast<IMFMediaEngineNotify *>(this);
            AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = InterlockedDecrement(&references);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR, DWORD error) override {
        if (auto h = target.load()) PostMessageW(h, eventMessage, event, LPARAM(error));
        return S_OK;
    }
};
static Notify *notification{};
static std::wstring version() { return wide(updates::currentVersion); }
static fs::path settings() { return appDirectory / L"settings.ini"; }
static bool automatic() {
    return launcher::readSetting(settings(), L"FeatureVideo", L"Automatic", L"1") != L"0";
}
static void logError(const char *stage, HRESULT result) {
    try {
        fs::create_directories(appDirectory / L"logs");
        std::ofstream out(appDirectory / L"logs/feature-video.txt", std::ios::app);
        out << stage << " HRESULT=0x" << std::hex << unsigned(result) << '\n';
    } catch (...) {}
}
static bool mediaAvailable() {
    try {
        auto manifest = appDirectory / L"media/feature-video.ini";
        auto expected = launcher::readSetting(manifest, L"Video", L"SHA256");
        if (!safeVersion(version()) || !validDigest(expected) ||
            launcher::readSetting(manifest, L"Video", L"Version") != version()) return false;
        mediaPath = appDirectory / L"media" / (version() + L".mp4");
        auto bytesText = launcher::readSetting(manifest, L"Video", L"Bytes");
        if (bytesText.empty() || bytesText.find_first_not_of(L"0123456789") != std::wstring::npos)
            return false;
        auto bytes = std::stoull(bytesText);
        return bytes >= 1024 && bytes <= 96 * 1024 * 1024 && fs::is_regular_file(mediaPath) &&
               fs::file_size(mediaPath) == bytes && wide(hash(mediaPath).c_str()) == expected;
    } catch (...) { return false; }
}
static void close() { if (dialog) DestroyWindow(dialog); }
static void fit() {
    if (!dialog || !video) return;
    RECT r{}; GetClientRect(dialog, &r);
    const int margin = theme::px(20), bar = theme::px(128),
              w = std::max(320, int(r.right) - margin * 2),
              h = std::max(160, int(r.bottom) - bar - margin * 2);
    SetWindowPos(video, nullptr, margin, margin, w, h, SWP_NOZORDER);
    if (extended) {
        RECT destination{0, 0, w, h};
        DWORD width{}, height{};
        if (SUCCEEDED(engine->GetNativeVideoSize(&width, &height)) && width && height) {
            int targetW = std::min(w, int(double(h) * width / height));
            int targetH = std::min(h, int(double(w) * height / width));
            destination = {(w - targetW) / 2, (h - targetH) / 2,
                           (w + targetW) / 2, (h + targetH) / 2};
        }
        MFARGB borderColor{0, 0, 0, 255};
        extended->UpdateVideoStream(nullptr, &destination, &borderColor);
    }
    int y = margin + h + theme::px(10);
    SetWindowPos(progress, nullptr, margin, y, w, theme::px(5), SWP_NOZORDER);
    y += theme::px(17);
    SetWindowPos(label, nullptr, margin, y, std::max(200, w - theme::px(345)), theme::px(24), SWP_NOZORDER);
    SetWindowPos(pause, nullptr, margin + w - theme::px(325), y - theme::px(4), theme::px(90), theme::px(32), SWP_NOZORDER);
    SetWindowPos(mute, nullptr, margin + w - theme::px(225), y - theme::px(4), theme::px(85), theme::px(32), SWP_NOZORDER);
    SetWindowPos(skip, nullptr, margin + w - theme::px(130), y - theme::px(4), theme::px(130), theme::px(32), SWP_NOZORDER);
    SetWindowPos(remember, nullptr, margin, y + theme::px(38), w, theme::px(30), SWP_NOZORDER);
}
static void failed(HRESULT result) {
    playbackFailed = true;
    logError("playback", result);
    SetWindowTextW(label, L"Video unavailable. Skip to continue; the text guide is in media/.");
    EnableWindow(pause, FALSE);
    if (engine) engine->Pause();
}
static void beginPlayback() {
    // Keep video optional on Windows N and systems without Media Foundation.
    // Static mfplat imports would prevent the entire launcher from starting.
    mediaFoundation = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (mediaFoundation) {
        startup = reinterpret_cast<decltype(startup)>(GetProcAddress(mediaFoundation, "MFStartup"));
        shutdown = reinterpret_cast<decltype(shutdown)>(GetProcAddress(mediaFoundation, "MFShutdown"));
        createAttributes = reinterpret_cast<decltype(createAttributes)>(GetProcAddress(mediaFoundation, "MFCreateAttributes"));
    }
    auto hr = startup && shutdown && createAttributes ? startup(MF_VERSION, MFSTARTUP_FULL)
                                                       : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    mfStarted = SUCCEEDED(hr);
    IMFAttributes *attributes{};
    IMFMediaEngineClassFactory *factory{};
    notification = new Notify;
    notification->target = dialog;
    if (SUCCEEDED(hr)) hr = createAttributes(&attributes, 3);
    if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notification);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, UINT64(UINT_PTR(video)));
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           __uuidof(IMFMediaEngineClassFactory), reinterpret_cast<void **>(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateInstance(0, attributes, &engine);
    if (factory) factory->Release();
    if (attributes) attributes->Release();
    if (SUCCEEDED(hr)) hr = engine->QueryInterface(__uuidof(IMFMediaEngineEx), reinterpret_cast<void **>(&extended));
    if (SUCCEEDED(hr)) {
        engine->SetVolume(0.65);
        bool silent = launcher::readSetting(settings(), L"FeatureVideo", L"Muted", L"0") == L"1";
        engine->SetMuted(silent);
        SetWindowTextW(mute, silent ? L"Unmute" : L"Mute");
        auto source = SysAllocString(mediaPath.c_str());
        hr = source ? engine->SetSource(source) : E_OUTOFMEMORY;
        SysFreeString(source);
    }
    if (FAILED(hr)) failed(hr);
}
static LRESULT CALLBACK procedure(HWND h, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        dialog = h;
        auto make = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style = 0) {
            return control(type, text, style | WS_TABSTOP, 0, 0, 100, 28, id, h);
        };
        video = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
                                0, 0, 1, 1, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        label = make(L"STATIC", L"Loading this version's feature video…", 701);
        pause = make(L"BUTTON", L"Pause", 702, BS_PUSHBUTTON);
        mute = make(L"BUTTON", L"Mute", 703, BS_PUSHBUTTON);
        skip = make(L"BUTTON", L"Skip / Continue", 704, BS_DEFPUSHBUTTON);
        remember = make(L"BUTTON", L"Show a feature video after future app updates", 705, BS_AUTOCHECKBOX);
        SendMessageW(remember, BM_SETCHECK, automatic() ? BST_CHECKED : BST_UNCHECKED, 0);
        progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                   0, 0, 1, 1, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(progress, PBM_SETRANGE32, 0, 1000);
        SendMessageW(progress, PBM_SETBARCOLOR, 0, theme::gold);
        theme::name(video, L"Feature video with open captions");
        theme::name(skip, L"Skip video and continue to the app");
        loaded = playbackFailed = false; startTick = GetTickCount64();
        fit(); SetFocus(skip);
        SetTimer(h, 1, 250, nullptr);
        PostMessageW(h, startMessage, 0, 0);
        return 0;
    }
    case startMessage:
        beginPlayback(); return 0;
    case eventMessage:
        if (w == MF_MEDIA_ENGINE_EVENT_CANPLAY && engine && !loaded && !playbackFailed) {
            loaded = true; fit(); engine->Play();
            SetWindowTextW(label, (L"What's new in " + version() + L"  ·  Captions included").c_str());
        } else if (w == MF_MEDIA_ENGINE_EVENT_ERROR) failed(HRESULT(l));
        else if (w == MF_MEDIA_ENGINE_EVENT_ENDED) close();
        return 0;
    case WM_TIMER:
        if (engine && loaded) {
            double duration = engine->GetDuration(), time = engine->GetCurrentTime();
            if (duration > 0 && std::isfinite(duration) && std::isfinite(time))
                SendMessageW(progress, PBM_SETPOS, WPARAM(std::clamp(time / duration * 1000.0, 0.0, 1000.0)), 0);
        } else if (!playbackFailed && GetTickCount64() - startTick > 15000) failed(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return 0;
    case WM_SIZE:
        fit(); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {theme::px(700), theme::px(500)};
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 704 || LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK) close();
        else if (LOWORD(w) == 702 && engine) {
            if (engine->IsPaused()) { engine->Play(); SetWindowTextW(pause, L"Pause"); }
            else { engine->Pause(); SetWindowTextW(pause, L"Play"); }
        } else if (LOWORD(w) == 703 && engine) {
            bool silent = !engine->GetMuted();
            engine->SetMuted(silent);
            SetWindowTextW(mute, silent ? L"Unmute" : L"Mute");
            launcher::writeSetting(L"FeatureVideo", L"Muted", silent ? L"1" : L"0", settings().c_str());
        } else if (LOWORD(w) == 705)
            launcher::writeSetting(L"FeatureVideo", L"Automatic",
                SendMessageW(remember, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", settings().c_str());
        return 0;
    case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: return theme::colors(message, w, l);
    case WM_DRAWITEM:
        if (theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT *>(l))) return TRUE;
        break;
    case WM_CLOSE: close(); return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        if (notification) notification->target = nullptr;
        if (engine) engine->Shutdown();
        if (extended) extended->Release();
        if (engine) engine->Release();
        if (notification) notification->Release();
        if (mfStarted && shutdown) shutdown();
        if (mediaFoundation) FreeLibrary(mediaFoundation);
        mediaFoundation = nullptr; startup = nullptr; shutdown = nullptr; createAttributes = nullptr;
        engine = nullptr; extended = nullptr; notification = nullptr; mfStarted = loaded = false;
        dialog = nullptr;
        if (owner && IsWindow(owner)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
static void show(HWND parent, bool automaticRequest) {
    if (dialog) { SetForegroundWindow(dialog); return; }
    if (running()) {
        if (!automaticRequest) MessageBoxW(parent, L"Close the simulator before playing the feature video.", L"What's new", MB_OK);
        return;
    }
    bool media = mediaAvailable();
    if (automaticRequest && !shouldPresent(automatic(), launcher::readSetting(settings(), L"FeatureVideo", L"LastPresented"), version(), media, false)) return;
    if (!media) {
        if (!automaticRequest) MessageBoxW(parent, L"This version's verified feature video is missing. Install the complete package from GitHub Releases.", L"What's new", MB_OK | MB_ICONINFORMATION);
        return;
    }
    WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"XPFSRFeatureVideo"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101)); wc.hbrBackground = theme::backgroundBrush;
    RegisterClassW(&wc);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::min(theme::px(1120), int(work.right - work.left) - 32);
    int height = std::min(theme::px(800), int(work.bottom - work.top) - 32);
    owner = parent;
    auto h = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, (L"What's new  ·  " + version()).c_str(),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             work.left + (work.right - work.left - width) / 2,
                             work.top + (work.bottom - work.top - height) / 2, width, height,
                             parent, nullptr, wc.hInstance, nullptr);
    if (!h) return;
    // Presentation counts once, including Skip/Close. A failed/missing integrity
    // check above never marks a video as presented. A same-version reinstall
    // retains this section because launcher saves only its own INI section.
    launcher::writeSetting(L"FeatureVideo", L"LastPresented", version().c_str(), settings().c_str());
    EnableWindow(parent, FALSE);
    ShowWindow(h, SW_SHOW); UpdateWindow(h);
}
} // namespace intro
