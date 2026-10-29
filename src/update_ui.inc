// SPDX-License-Identifier: MIT
#include "update_http.hpp"
#include <mutex>
#include <thread>
struct UpdateJob {
    std::mutex mutex;
    enum Phase { idle, checking, available, current, downloading, ready, failed } phase{idle};
    std::atomic<bool> cancel{};
    std::atomic<uint64_t> bytes{};
    updates::Release release;
    std::wstring message{L"Checks use this project's public GitHub Releases. No account required."};
    fs::path installer, directory;
    bool handedOff{};
    ~UpdateJob() {
        if (!handedOff && !directory.empty()) {
            std::error_code ec;
            fs::remove(directory / L"setup.part", ec);
            fs::remove(directory / L"setup.exe", ec);
            fs::remove(directory, ec);
        }
    }
};
static std::shared_ptr<UpdateJob> updateJob = std::make_shared<UpdateJob>();
static HWND updatesButton{}, updateWindow{}, updateText{}, autoCheckBox{}, previewCheckBox{},
    checkUpdateButton{}, installUpdateButton{}, releaseNotesButton{};
static bool autoCheckUpdates{true}, previewUpdates{true};
static bool ownedInstallation() {
    return launcher::readSetting(appDirectory / L"XPFSR24-Installation.ini", L"Installation",
                                 L"Product") == L"XPFSR24-community-alpha";
}
static void refreshUpdateUI() {
    auto j = updateJob;
    std::lock_guard<std::mutex> guard(j->mutex);
    bool busy = j->phase == UpdateJob::checking || j->phase == UpdateJob::downloading ||
                j->phase == UpdateJob::ready;
    std::wstring title = j->phase == UpdateJob::available ? L"Update available..." : L"Updates...";
    SetWindowTextW(updatesButton, title.c_str());
    if (!updateWindow)
        return;
    auto message =
        L"Installed version: " + wide(updates::currentVersion) + L"\r\n\r\n" + j->message;
    if (j->phase == UpdateJob::downloading)
        message += L"\r\nDownloaded " + std::to_wstring(j->bytes / 1024) + L" / " +
                   std::to_wstring(j->release.bytes / 1024) + L" KiB";
    if (j->release) {
        message += L"\r\n\r\nAvailable: " + wide(j->release.tag.c_str()) + L"\r\n" +
                   wide(j->release.name.c_str());
    }
    if (!ownedInstallation())
        message += L"\r\n\r\nFor automatic installation, first install the app using the official "
                   L"setup package. Use Open release for this portable copy.";
    if (text(updateText) != message)
        SetWindowTextW(updateText, message.c_str());
    EnableWindow(checkUpdateButton, !busy);
    EnableWindow(autoCheckBox, !busy);
    EnableWindow(previewCheckBox, !busy);
    EnableWindow(installUpdateButton, j->phase == UpdateJob::available && ownedInstallation());
    EnableWindow(releaseNotesButton, bool(j->release));
}
static void startUpdateCheck() {
    {
        std::lock_guard<std::mutex> guard(updateJob->mutex);
        if (updateJob->phase == UpdateJob::checking || updateJob->phase == UpdateJob::downloading ||
            updateJob->phase == UpdateJob::ready)
            return;
    }
    auto j = std::make_shared<UpdateJob>();
    updateJob = j;
    j->phase = UpdateJob::checking;
    j->message = L"Checking GitHub Releases... You can keep using the launcher.";
    bool previews = previewUpdates;
    std::thread([j, previews] {
        try {
            auto release = updates::check(updates::currentVersion, previews, j->cancel, j->bytes);
            std::lock_guard<std::mutex> guard(j->mutex);
            j->release = std::move(release);
            j->phase = j->release ? UpdateJob::available : UpdateJob::current;
            j->message = j->release
                             ? L"An update is available. Open release to read its changes. "
                               L"Download and install closes/reopens this launcher and preserves "
                               L"saved choices. Close every X-Plane and GPU-check process first."
                             : L"No newer compatible installer is published for your selected "
                               L"channel. Updates never downgrade this installation.";
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::failed;
            j->message = wide(e.what()) + L"\r\nNo files in the application or simulator were "
                                          L"changed. Use Check now to retry.";
        }
    }).detach();
    refreshUpdateUI();
}
static void beginUpdateInstall() {
    ensure(ownedInstallation(),
           "Install the app with the official setup package before using automatic installation");
    graphics::ensureSimulatorClosed();
    ensure(!running() && (!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT),
           "Close X-Plane and all GPU checks before updating");
    saveChoices();
    auto j = updateJob;
    {
        std::lock_guard<std::mutex> guard(j->mutex);
        if (j->phase != UpdateJob::available)
            return;
        j->phase = UpdateJob::downloading;
        j->message = L"Downloading the installer over HTTPS. SHA-256 must match GitHub before it "
                     L"can run. Close this window to cancel.";
        j->cancel = false;
        j->bytes = 0;
    }
    std::thread([j] {
        try {
            GUID id{};
            updates::require(SUCCEEDED(CoCreateGuid(&id)),
                             "Could not allocate an update download folder");
            wchar_t name[40]{};
            StringFromGUID2(id, name, 40);
            j->directory = fs::temp_directory_path() / (L"XPFSR24-update-" + std::wstring(name));
            updates::require(fs::create_directory(j->directory),
                             "Could not create a private update staging folder");
            auto part = j->directory / L"setup.part";
            updates::transfer(updates::widen(j->release.assetURL), part, j->release.bytes,
                              j->cancel, j->bytes);
            updates::verify(part, j->release);
            updates::require(!j->cancel, "Update canceled");
            j->installer = j->directory / L"setup.exe";
            fs::rename(part, j->installer);
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::ready;
            j->message = L"Download verified. Installing the update and reopening the launcher...";
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::failed;
            j->message = wide(e.what()) + L"\r\nNo update was installed. Use Check now to retry.";
        }
    }).detach();
    refreshUpdateUI();
}
static void pollUpdate() {
    refreshUpdateUI();
    auto j = updateJob;
    std::lock_guard<std::mutex> guard(j->mutex);
    if (j->phase != UpdateJob::ready)
        return;
    try {
        updates::require(!j->cancel, "Update canceled");
        graphics::ensureSimulatorClosed();
        ensure(!running() && (!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT),
               "Close all GPU checks before updating");
        saveChoices();
        updates::verify(j->installer, j->release);
        std::wstring args = L"/S /AUTOUPDATE=1 /PARENT=" + std::to_wstring(GetCurrentProcessId()) +
                            L" /D=" + appDirectory.wstring();
        SHELLEXECUTEINFOW start{sizeof(start)};
        start.fMask = SEE_MASK_NOCLOSEPROCESS;
        start.hwnd = window;
        start.lpVerb = L"open";
        start.lpFile = j->installer.c_str();
        start.lpParameters = args.c_str();
        start.nShow = SW_SHOWNORMAL;
        ensure(ShellExecuteExW(&start) != FALSE,
               "Windows could not start the verified installer; no application files were changed");
        if (start.hProcess)
            CloseHandle(start.hProcess);
        j->handedOff = true;
        j->phase = UpdateJob::idle;
        if (updateWindow)
            DestroyWindow(updateWindow);
        PostMessageW(window, WM_CLOSE, 0, 0);
    } catch (const std::exception &e) {
        j->phase = UpdateJob::failed;
        j->message = wide(e.what()) + L"\r\nNo update was installed.";
    }
}
static LRESULT CALLBACK updateProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_COMMAND:
        try {
            switch (LOWORD(w)) {
            case 150:
            case 151: {
                const wchar_t *key = LOWORD(w) == 150 ? L"AutomaticChecks" : L"PreviewReleases";
                bool value = SendMessageW(LOWORD(w) == 150 ? autoCheckBox : previewCheckBox,
                                          BM_GETCHECK, 0, 0) == BST_CHECKED;
                ensure(WritePrivateProfileStringW(L"Updates", key, value ? L"1" : L"0",
                                                  (appDirectory / L"settings.ini").c_str()),
                       "Could not save the update preference");
                WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                                           (appDirectory / L"settings.ini").c_str());
                ensure(launcher::readSetting(appDirectory / L"settings.ini", L"Updates", key) ==
                           (value ? L"1" : L"0"),
                       "Saved update preference could not be verified");
                (LOWORD(w) == 150 ? autoCheckUpdates : previewUpdates) = value;
                if (LOWORD(w) == 151)
                    startUpdateCheck();
                break;
            }
            case 152:
                startUpdateCheck();
                break;
            case 153:
                beginUpdateInstall();
                break;
            case 154: {
                std::lock_guard<std::mutex> guard(updateJob->mutex);
                if (updateJob->release)
                    ShellExecuteW(hwnd, L"open", wide(updateJob->release.url.c_str()).c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            case 155:
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
                break;
            }
        } catch (const std::exception &e) {
            SendMessageW(autoCheckBox, BM_SETCHECK, autoCheckUpdates ? BST_CHECKED : BST_UNCHECKED,
                         0);
            SendMessageW(previewCheckBox, BM_SETCHECK, previewUpdates ? BST_CHECKED : BST_UNCHECKED,
                         0);
            MessageBoxW(hwnd, wide(e.what()).c_str(), L"Application updates",
                        MB_OK | MB_ICONEXCLAMATION);
        }
        return 0;
    case WM_CLOSE: {
        std::lock_guard<std::mutex> guard(updateJob->mutex);
        if (updateJob->phase == UpdateJob::downloading || updateJob->phase == UpdateJob::ready)
            updateJob->cancel = true;
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        updateWindow = nullptr;
        EnableWindow(window, TRUE);
        SetForegroundWindow(window);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
static void showUpdates() {
    if (updateWindow) {
        SetForegroundWindow(updateWindow);
        return;
    }
    WNDCLASSW c{};
    c.lpfnWndProc = updateProcedure;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"XPFSR24Updates";
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassW(&c);
    updateWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName, L"Application updates",
                                   WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                   CW_USEDEFAULT, 700, 490, window, nullptr, c.hInstance, nullptr);
    ensure(updateWindow != nullptr, "Could not open application updates");
    autoCheckBox = control(L"BUTTON", L"Automatically check for updates when the app opens",
                           BS_AUTOCHECKBOX | WS_TABSTOP, 20, 20, 645, 28, 150, updateWindow);
    previewCheckBox = control(L"BUTTON", L"Include alpha / beta / release-candidate previews",
                              BS_AUTOCHECKBOX | WS_TABSTOP, 20, 54, 645, 28, 151, updateWindow);
    SendMessageW(autoCheckBox, BM_SETCHECK, autoCheckUpdates ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(previewCheckBox, BM_SETCHECK, previewUpdates ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"STATIC",
            L"Preview builds are experimental. Installation requires your click; it never "
            L"interrupts a flight.",
            0, 20, 92, 645, 40, 0, updateWindow);
    updateText = control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 140, 645, 195,
                         0, updateWindow);
    checkUpdateButton = control(L"BUTTON", L"Check now", BS_PUSHBUTTON | WS_TABSTOP, 20, 350, 145,
                                36, 152, updateWindow);
    releaseNotesButton = control(L"BUTTON", L"Open release", BS_PUSHBUTTON | WS_TABSTOP, 180, 350,
                                 145, 36, 154, updateWindow);
    installUpdateButton = control(L"BUTTON", L"Download and install", BS_PUSHBUTTON | WS_TABSTOP,
                                  340, 350, 325, 36, 153, updateWindow);
    control(L"BUTTON", L"Close / cancel download", BS_PUSHBUTTON | WS_TABSTOP, 420, 401, 245, 32,
            155, updateWindow);
    refreshUpdateUI();
    EnableWindow(window, FALSE);
    ShowWindow(updateWindow, SW_SHOW);
    SetFocus(checkUpdateButton);
    MSG msg{};
    while (updateWindow && GetMessageW(&msg, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(updateWindow, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
}
