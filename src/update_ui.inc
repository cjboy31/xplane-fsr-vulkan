// SPDX-License-Identifier: MIT
#include "update_http.hpp"
#include "update_preferences.hpp"
#include <mutex>
#include <thread>
struct UpdateJob {
    std::mutex mutex;
    enum Phase { idle, checking, available, current, downloading, ready, failed } phase{idle};
    std::atomic<bool> cancel{};
    std::atomic<uint64_t> bytes{};
    updates::Release release;
    updates::Channel channel{updates::installedChannel()};
    std::wstring message{L"Checks use this project's public GitHub Releases. No account required."};
    fs::path installer, directory, settingsSnapshot;
    std::string settingsDigest;
    bool handedOff{}, automaticInstall{};
    ~UpdateJob() {
        if (!handedOff && !directory.empty()) {
            std::error_code ec;
            fs::remove(directory / L"setup.part", ec);
            fs::remove(directory / L"setup.exe", ec);
            fs::remove(directory / L"settings.ini", ec);
            fs::remove(directory, ec);
        }
    }
};
static std::shared_ptr<UpdateJob> updateJob = std::make_shared<UpdateJob>();
static HWND updatesButton{}, updateWindow{}, updateText{}, autoCheckBox{}, channelBox{},
    checkUpdateButton{}, installUpdateButton{}, releaseNotesButton{};
static bool autoCheckUpdates{true};
static updates::Channel releaseChannel{updates::installedChannel()};
// Kept as a dependency so the full dialog state machine can be tested with
// delayed/failing release responses without contacting or executing anything.
static auto queryUpdateRelease = updates::check;
static auto downloadUpdateFile = updates::transfer;
static decltype(&ShellExecuteExW) runUpdateInstaller = ShellExecuteExW;
static fs::path (*updateStagingRoot)() = [] { return fs::temp_directory_path(); };
static bool ownedInstallation() {
    return launcher::readSetting(appDirectory / L"XPFSR24-Installation.ini", L"Installation",
                                 L"Product") == L"XPFSR24-community-alpha";
}
static bool supportsChoicesImport(const updates::Release &release) {
    if (!release)
        return false;
    auto v = updates::version(release.tag);
    return std::tie(v.major, v.minor, v.patch) >= std::make_tuple(0, 6, 6);
}
static void refreshUpdateUI() {
    auto j = updateJob;
    std::lock_guard<std::mutex> guard(j->mutex);
    bool busy = j->phase == UpdateJob::checking || j->phase == UpdateJob::downloading ||
                j->phase == UpdateJob::ready;
    std::wstring title =
        j->phase == UpdateJob::available ? L"Update available..." : L"Updates / Channel";
    SetWindowTextW(updatesButton, title.c_str());
    if (!updateWindow)
        return;
    bool matching = j->channel == releaseChannel;
    bool downloadable = matching && bool(j->release) &&
                        (j->phase == UpdateJob::available || j->phase == UpdateJob::failed);
    bool automatic = ownedInstallation();
    auto message = L"Installed: " + wide(updates::currentVersion) + L" (" +
                   updates::channelName(updates::installedChannel()) + L")\r\nSelected channel: " +
                   updates::channelName(releaseChannel) + L"\r\n\r\n";
    if (downloadable && !automatic)
        message += L"Setup needed: this copy has no recognized installation marker. "
                   L"The button below downloads verified setup and opens its wizard. "
                   L"Choose an empty application folder or an existing setup-installed folder. "
                   L"Existing installation settings are kept.\r\n\r\n";
    if (downloadable && !automatic)
        message += supportsChoicesImport(j->release)
                       ? L"New installations import your current saved choices.\r\n\r\n"
                       : L"This older setup cannot import choices automatically. They remain "
                         L"in this copy's settings.ini; copy it to the new app folder with "
                         L"both launchers closed if needed.\r\n\r\n";
    message += matching ? j->message : L"Check the selected channel before installing.";
    if (j->phase == UpdateJob::downloading)
        message += L"\r\nDownloaded " + std::to_wstring(j->bytes / 1024) + L" / " +
                   std::to_wstring(j->release.bytes / 1024) + L" KiB";
    if (j->release && matching) {
        message += L"\r\n\r\nAvailable: " + wide(j->release.tag.c_str()) + L"\r\n" +
                   wide(j->release.name.c_str());
        if (j->release.channelSwitch)
            message += L"\r\nThis changes release channels and may install an older version. "
                       L"Experimental FG may flicker or show artifacts; it is not a quality fix. "
                       L"Use the install button below to change builds.";
    }
    if (text(updateText) != message)
        SetWindowTextW(updateText, message.c_str());
    EnableWindow(checkUpdateButton, !busy);
    EnableWindow(autoCheckBox, !busy);
    EnableWindow(channelBox, j->phase != UpdateJob::downloading && j->phase != UpdateJob::ready);
    SetWindowTextW(installUpdateButton,
                   j->release.channelSwitch
                       ? (automatic ? L"Switch and install" : L"Switch using setup...")
                       : (automatic ? L"Download and install" : L"Install using setup..."));
    EnableWindow(installUpdateButton, downloadable);
    EnableWindow(releaseNotesButton, bool(j->release) && matching);
}
static void startUpdateCheck() {
    {
        std::lock_guard<std::mutex> guard(updateJob->mutex);
        if ((updateJob->phase == UpdateJob::checking && updateJob->channel == releaseChannel) ||
            updateJob->phase == UpdateJob::downloading || updateJob->phase == UpdateJob::ready)
            return;
        updateJob->cancel = true;
    }
    auto j = std::make_shared<UpdateJob>();
    updateJob = j;
    j->phase = UpdateJob::checking;
    j->message = L"Checking GitHub Releases... You can keep using the launcher.";
    j->channel = releaseChannel;
    auto notifyWindow = window;
    std::thread([j, notifyWindow] {
        try {
            auto release =
                queryUpdateRelease(updates::currentVersion, j->channel, j->cancel, j->bytes);
            std::lock_guard<std::mutex> guard(j->mutex);
            j->release = std::move(release);
            j->phase = j->release ? UpdateJob::available : UpdateJob::current;
            j->message = j->release
                             ? L"An update is available. Open release to read its changes. "
                               L"Download and install closes/reopens this launcher and preserves "
                               L"saved choices. Close every X-Plane and GPU-check process first."
                         : j->channel == updates::installedChannel()
                             ? L"This channel is already installed. No newer compatible installer "
                               L"is published. Choose the other channel to switch builds. "
                               L"Ordinary updates never downgrade within a channel."
                             : L"No compatible installer is published for your selected "
                               L"channel. Ordinary updates never downgrade within a channel. "
                               L"Normal includes this project's alpha releases and current NR/FG features.";
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::failed;
            j->message = wide(e.what()) + L"\r\nNo files in the application or simulator were "
                                          L"changed. Use Check now to retry.";
        }
        PostMessageW(notifyWindow, WM_TIMER, 4, 0);
    }).detach();
    refreshUpdateUI();
}
static void beginUpdateInstall() {
    ensure(!nrJobRunning(),"Wait for NR import/check to finish before updating");
    graphics::ensureSimulatorClosed();
    ensure(!running() && (!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT),
           "Close X-Plane and all GPU checks before updating");
    saveChoices();
    auto previous = updateJob;
    auto j = std::make_shared<UpdateJob>();
    {
        std::lock_guard<std::mutex> guard(previous->mutex);
        if (previous->phase != UpdateJob::available && previous->phase != UpdateJob::failed)
            return;
        ensure(previous->release && previous->channel == releaseChannel,
               "Check the selected channel again before installing");
        j->release = previous->release;
        j->channel = previous->channel;
        j->automaticInstall = ownedInstallation();
        j->phase = UpdateJob::downloading;
        j->message = L"Downloading the installer over HTTPS. SHA-256 must match GitHub before it "
                     L"can run. Close this window to cancel.";
        j->cancel = false;
        j->bytes = 0;
    }
    updateJob = j;
    auto notifyWindow = window;
    std::thread([j, notifyWindow] {
        try {
            GUID id{};
            updates::require(SUCCEEDED(CoCreateGuid(&id)),
                             "Could not allocate an update download folder");
            wchar_t name[40]{};
            StringFromGUID2(id, name, 40);
            j->directory = updateStagingRoot() / (L"XPFSR24-update-" + std::wstring(name));
            updates::require(fs::create_directory(j->directory),
                             "Could not create a private update staging folder");
            if (!j->automaticInstall) {
                j->settingsSnapshot = j->directory / L"settings.ini";
                fs::copy_file(appDirectory / L"settings.ini", j->settingsSnapshot);
                j->settingsDigest = updates::sha256(j->settingsSnapshot);
            }
            auto part = j->directory / L"setup.part";
            downloadUpdateFile(updates::widen(j->release.assetURL), part, j->release.bytes,
                               j->cancel, j->bytes);
            updates::verify(part, j->release);
            updates::require(!j->cancel, "Update canceled");
            j->installer = j->directory / L"setup.exe";
            fs::rename(part, j->installer);
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::ready;
            j->message =
                j->automaticInstall
                    ? L"Download verified. Installing and reopening the launcher..."
                    : L"Download verified. Opening setup to choose the installation folder...";
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> guard(j->mutex);
            j->phase = UpdateJob::failed;
            j->message = wide(e.what()) + L"\r\nNo update was installed. Use Check now to retry.";
        }
        PostMessageW(notifyWindow, WM_TIMER, 4, 0);
    }).detach();
    refreshUpdateUI();
}
static std::wstring updateInstallArguments(const UpdateJob &j, DWORD parent) {
    if (j.automaticInstall) {
        ensure(ownedInstallation(), "The installation marker changed. Run setup manually.");
        return L"/S /AUTOUPDATE=1 /PARENT=" + std::to_wstring(parent) + L" /D=" +
               appDirectory.wstring();
    }
    ensure(!j.settingsSnapshot.empty() && updates::sha256(j.settingsSnapshot) == j.settingsDigest,
           "Saved choices changed during download. Check again before opening setup.");
    // No silent or automatic mode and no /D= for an unrecognized directory.
    return supportsChoicesImport(j.release)
               ? L"/IMPORTSETTINGS=\"" + j.settingsSnapshot.wstring() + L"\""
               : L"";
}
static void pollUpdate() {
    refreshUpdateUI();
    auto j = updateJob;
    std::lock_guard<std::mutex> guard(j->mutex);
    if (j->phase != UpdateJob::ready)
        return;
    try {
        updates::require(!j->cancel, "Update canceled");
        ensure(!nrJobRunning(),"Wait for NR import/check to finish before updating");
        graphics::ensureSimulatorClosed();
        ensure(!running() && (!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT),
               "Close all GPU checks before updating");
        saveChoices();
        updates::verify(j->installer, j->release);
        auto args = updateInstallArguments(*j, GetCurrentProcessId());
        SHELLEXECUTEINFOW start{sizeof(start)};
        start.fMask = SEE_MASK_NOCLOSEPROCESS;
        start.hwnd = window;
        start.lpVerb = L"open";
        start.lpFile = j->installer.c_str();
        start.lpParameters = args.c_str();
        start.nShow = SW_SHOWNORMAL;
        ensure(runUpdateInstaller(&start) != FALSE,
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
    case WM_DRAWITEM:
        if (theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT *>(l)))
            return TRUE;
        break;
    case WM_MEASUREITEM:
        if (auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(l);
            item && item->CtlType == ODT_COMBOBOX) {
            item->itemHeight = theme::px(30);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        try {
            switch (LOWORD(w)) {
            case 150: {
                const wchar_t *key = L"AutomaticChecks";
                bool value = SendMessageW(autoCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                ensure(WritePrivateProfileStringW(L"Updates", key, value ? L"1" : L"0",
                                                  (appDirectory / L"settings.ini").c_str()),
                       "Could not save the update preference");
                WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                                           (appDirectory / L"settings.ini").c_str());
                ensure(launcher::readSetting(appDirectory / L"settings.ini", L"Updates", key) ==
                           (value ? L"1" : L"0"),
                       "Saved update preference could not be verified");
                autoCheckUpdates = value;
                break;
            }
            case 151: {
                if (HIWORD(w) != CBN_SELCHANGE)
                    break;
                auto selected = SendMessageW(channelBox, CB_GETCURSEL, 0, 0);
                ensure(selected == 0 || selected == 1, "Choose a release channel");
                auto next =
                    selected == 1 ? updates::Channel::Experimental : updates::Channel::Normal;
                updates::saveChannel(appDirectory / L"settings.ini", next);
                releaseChannel = next;
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
            SendMessageW(channelBox, CB_SETCURSEL,
                         releaseChannel == updates::Channel::Experimental ? 1 : 0, 0);
            MessageBoxW(hwnd, wide(e.what()).c_str(), L"Application updates",
                        MB_OK | MB_ICONEXCLAMATION);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return theme::colors(message, w, l);
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
    c.hbrBackground = theme::cardBrush;
    RegisterClassW(&c);
    updateWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName, L"Application updates",
                                   WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                   CW_USEDEFAULT, modalSize(700, 550).cx, modalSize(700, 550).cy,
                                   window, nullptr, c.hInstance, nullptr);
    ensure(updateWindow != nullptr, "Could not open application updates");
    // Keep the modal on the owner's monitor rather than relying on a stale
    // Windows default placement (which can put it partly outside the display).
    RECT owner{}, bounds{};
    GetWindowRect(window, &owner);
    GetWindowRect(updateWindow, &bounds);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    int x = std::clamp(int(owner.left + (owner.right - owner.left - width) / 2),
                       int(monitor.rcWork.left),
                       int(std::max(monitor.rcWork.left, monitor.rcWork.right - width)));
    int y = std::clamp(int(owner.top + (owner.bottom - owner.top - height) / 2),
                       int(monitor.rcWork.top),
                       int(std::max(monitor.rcWork.top, monitor.rcWork.bottom - height)));
    SetWindowPos(updateWindow, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    autoCheckBox = scaledControl(L"BUTTON", L"Automatically check for updates when the app opens",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 20, 20, 645, 28, 150, updateWindow);
    scaledControl(L"STATIC", L"Release channel", 0, 20, 60, 120, 24, 0, updateWindow);
    channelBox = scaledControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 155, 54, 510, 150,
                               151, updateWindow);
    SendMessageW(channelBox, CB_ADDSTRING, 0,
                 LPARAM(L"Normal - all current features (recommended)"));
    SendMessageW(channelBox, CB_ADDSTRING, 0,
                 LPARAM(L"Experimental - early development builds"));
    SendMessageW(autoCheckBox, BM_SETCHECK, autoCheckUpdates ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(channelBox, CB_SETCURSEL, releaseChannel == updates::Channel::Experimental ? 1 : 0,
                 0);
    theme::name(channelBox, L"Release channel: Normal or Experimental");
    scaledControl(L"STATIC",
                  L"Normal includes current NR, FG and compatibility options. Experimental "
                  L"receives future development builds. Hardware/model requirements still apply. Selecting a channel only "
                  L"checks releases. Install explicitly, with X-Plane closed.",
                  0, 20, 96, 645, 64, 0, updateWindow);
    updateText = scaledControl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 170, 645,
                               220, 0, updateWindow);
    checkUpdateButton = scaledControl(L"BUTTON", L"Check now", BS_PUSHBUTTON | WS_TABSTOP, 20, 408,
                                      145, 36, 152, updateWindow);
    releaseNotesButton = scaledControl(L"BUTTON", L"Open release", BS_PUSHBUTTON | WS_TABSTOP, 180,
                                       408, 145, 36, 154, updateWindow);
    installUpdateButton =
        scaledControl(L"BUTTON", L"Download and install", BS_PUSHBUTTON | WS_TABSTOP, 340, 408, 325,
                      36, 153, updateWindow);
    scaledControl(L"BUTTON", L"Close / cancel download", BS_PUSHBUTTON | WS_TABSTOP, 420, 463, 245,
                  32, 155, updateWindow);
    theme::caption(updateWindow);
    refreshUpdateUI();
    bool firstCheck{};
    {
        std::lock_guard<std::mutex> guard(updateJob->mutex);
        firstCheck = updateJob->phase == UpdateJob::idle;
    }
    if (firstCheck)
        startUpdateCheck();
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
