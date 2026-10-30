// SPDX-License-Identifier: MIT
// Real updater commands, workers, controls, verification and handoff arguments.
// Only network and process launch are replaced. No desktop input or real setup.
#define wWinMain unusedLauncherMain
#include "launcher.cpp"
#undef wWinMain
#include <iostream>

static fs::path fixtureRoot;
static std::string payload(2048, '\0'), payloadDigest;
static std::atomic<int> queryMode{}, queryCalls{}, activeQueries{}, downloadMode{};
static std::atomic<bool> releaseOldQuery{}, downloadStarted{};
static std::wstring launchedArguments;
static int launchCount{};
static bool launchSucceeds{true};

template <class F> static void until(F predicate, const char *why) {
    auto start = GetTickCount64();
    while (!predicate()) {
        ensure(GetTickCount64() - start < 5000, why);
        Sleep(5);
    }
}
static void waitPhase() {
    until(
        [] {
            std::lock_guard<std::mutex> guard(updateJob->mutex);
            return updateJob->phase != UpdateJob::checking &&
                   updateJob->phase != UpdateJob::downloading;
        },
        "Updater worker stalled");
    refreshUpdateUI();
}
static updates::Release fakeRelease(updates::Channel requested) {
    std::string tag =
        requested == updates::Channel::Experimental ? "v99.0.0-experimental.1" : "v99.0.0-alpha";
    return {tag,
            "Fixture release",
            "Fixture notes",
            std::string(updates::repository) + "/releases/tag/" + tag,
            "https://github.com/fixture",
            "sha256:" + payloadDigest,
            payload.size(),
            requested != updates::installedChannel()};
}
static updates::Release fakeQuery(const std::string &, updates::Channel requested,
                                  std::atomic<bool> &, std::atomic<uint64_t> &) {
    ++activeQueries;
    struct End {
        ~End() { --activeQueries; }
    } end;
    int call = ++queryCalls, mode = queryMode;
    if (mode == 1)
        throw std::runtime_error("Fixture connection failure");
    if (mode == 2 && call == 1)
        until([] { return bool(releaseOldQuery); }, "Old query was not released");
    if (mode == 3)
        return {};
    return fakeRelease(requested);
}
static std::string fakeDownload(std::wstring, const fs::path &destination, uint64_t,
                                std::atomic<bool> &cancel, std::atomic<uint64_t> &progress) {
    downloadStarted = true;
    if (downloadMode == 1)
        throw std::runtime_error("Fixture interrupted download");
    if (downloadMode == 3) {
        until([&] { return bool(cancel); }, "Download cancellation did not arrive");
        throw std::runtime_error("Update canceled");
    }
    auto data = payload;
    if (downloadMode == 2)
        data[100] = 'X';
    std::ofstream out(destination, std::ios::binary);
    out.write(data.data(), data.size());
    out.close();
    progress = data.size();
    return {};
}
static BOOL WINAPI fakeLaunch(SHELLEXECUTEINFOW *start) {
    ++launchCount;
    launchedArguments = start->lpParameters;
    ensure(fs::exists(start->lpFile), "Handoff installer is missing");
    start->hProcess = nullptr;
    return launchSucceeds;
}
static void dialog() {
    updateWindow = CreateWindowW(L"XPFSRUpdateFlowFixture", L"Hidden updater", 0, 0, 0, 900, 550,
                                 window, nullptr, GetModuleHandleW(nullptr), nullptr);
    ensure(updateWindow != nullptr, "Cannot create updater fixture");
    updateText = scaledControl(L"EDIT", L"", ES_MULTILINE, 20, 170, 645, 220, 0, updateWindow);
    autoCheckBox =
        scaledControl(L"BUTTON", L"Automatic", BS_AUTOCHECKBOX, 20, 20, 200, 28, 150, updateWindow);
    channelBox =
        scaledControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 155, 54, 510, 150, 151, updateWindow);
    SendMessageW(channelBox, CB_ADDSTRING, 0, LPARAM(L"Normal"));
    SendMessageW(channelBox, CB_ADDSTRING, 0, LPARAM(L"Experimental"));
    checkUpdateButton =
        scaledControl(L"BUTTON", L"Check now", BS_PUSHBUTTON, 20, 408, 145, 36, 152, updateWindow);
    releaseNotesButton = scaledControl(L"BUTTON", L"Open release", BS_PUSHBUTTON, 180, 408, 145, 36,
                                       154, updateWindow);
    installUpdateButton =
        scaledControl(L"BUTTON", L"Install", BS_PUSHBUTTON, 340, 408, 325, 36, 153, updateWindow);
}
static void ownership(const wchar_t *value) {
    WritePrivateProfileStringW(L"Installation", L"Product", value,
                               (appDirectory / L"XPFSR24-Installation.ini").c_str());
}
static void available(updates::Channel selected) {
    updateJob = std::make_shared<UpdateJob>();
    releaseChannel = selected;
    updateJob->channel = selected;
    updateJob->release = fakeRelease(selected);
    updateJob->phase = UpdateJob::available;
    refreshUpdateUI();
}
template <class F> static void rejects(F action, const char *why) {
    bool rejected{};
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    ensure(rejected, why);
}
int wmain(int argc, wchar_t **argv) {
    try {
        ensure(argc == 2 && !fs::exists(argv[1]), "Fresh workspace fixture required");
        fixtureRoot = argv[1];
        appDirectory = fixtureRoot / L"portable app";
        fs::create_directories(appDirectory);
        payload[0] = 'M';
        payload[1] = 'Z';
        auto reference = fixtureRoot / L"reference.exe";
        std::ofstream(reference, std::ios::binary).write(payload.data(), payload.size());
        payloadDigest = updates::sha256(reference);
        launcher::Preferences choices;
        choices.mode = 4;
        choices.frameGeneration = 2;
        choices.presetId = "fsr4-4k-quality";
        launcher::save(appDirectory / L"settings.ini", choices);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        window = CreateWindowW(L"STATIC", L"Hidden owner", 0, 0, 0, 1, 1, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
        theme::initialize(window);
        WNDCLASSW c{};
        c.lpfnWndProc = updateProcedure;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"XPFSRUpdateFlowFixture";
        RegisterClassW(&c);
        updatesButton = CreateWindowW(L"BUTTON", L"Updates", WS_CHILD, 0, 0, 1, 1, window, nullptr,
                                      c.hInstance, nullptr);
        queryUpdateRelease = fakeQuery;
        downloadUpdateFile = fakeDownload;
        runUpdateInstaller = fakeLaunch;
        updateStagingRoot = [] { return fixtureRoot; };
        dialog();
        for (auto selected : {updates::Channel::Normal, updates::Channel::Experimental}) {
            for (auto marker : {L"", L"unrelated-product", L"XPFSR24-community-alpha"}) {
                ownership(marker);
                available(selected);
                ensure(IsWindowEnabled(installUpdateButton),
                       "Available release is a greyed-out dead end");
                bool owned = !wcscmp(marker, L"XPFSR24-community-alpha");
                ensure((text(installUpdateButton).find(L"setup") != std::wstring::npos) == !owned,
                       "Install action does not match setup ownership");
                for (auto phase : {UpdateJob::idle, UpdateJob::checking, UpdateJob::current,
                                   UpdateJob::downloading, UpdateJob::ready}) {
                    updateJob->phase = phase;
                    refreshUpdateUI();
                    ensure(!IsWindowEnabled(installUpdateButton),
                           "Install enabled in a busy/current state");
                }
                updateJob->phase = UpdateJob::failed;
                refreshUpdateUI();
                ensure(IsWindowEnabled(installUpdateButton),
                       "Verified release cannot retry after failure");
            }
        }
        std::cout << "PASS: install-action/ownership/state matrix in both channel directions\n";
        available(updates::Channel::Normal);
        releaseChannel = updates::Channel::Experimental;
        refreshUpdateUI();
        ensure(!IsWindowEnabled(installUpdateButton) && !IsWindowEnabled(releaseNotesButton),
               "A stale result offers the wrong channel");
        rejects([] { beginUpdateInstall(); }, "Stale channel installation was accepted");
        queryMode = 2;
        queryCalls = 0;
        releaseOldQuery = false;
        updateJob = std::make_shared<UpdateJob>();
        releaseChannel = updates::Channel::Normal;
        startUpdateCheck();
        until([] { return queryCalls == 1; }, "First query did not start");
        auto oldQuery = updateJob;
        SendMessageW(channelBox, CB_SETCURSEL, 1, 0);
        SendMessageW(updateWindow, WM_COMMAND, MAKEWPARAM(151, CBN_SELCHANGE), LPARAM(channelBox));
        waitPhase();
        ensure(oldQuery->cancel && updateJob->channel == updates::Channel::Experimental &&
                   IsWindowEnabled(installUpdateButton),
               "Rapid channel switch failed");
        releaseOldQuery = true;
        until([] { return activeQueries == 0; }, "Query worker did not finish");
        ensure(updateJob->channel == updates::Channel::Experimental &&
                   updates::loadChannel(appDirectory / L"settings.ini") == releaseChannel,
               "An old query overwrote the selected channel");
        oldQuery.reset();
        queryMode = 1;
        startUpdateCheck();
        waitPhase();
        ensure(IsWindowEnabled(checkUpdateButton) && !IsWindowEnabled(installUpdateButton),
               "Failed check cannot recover safely");
        queryMode = 3;
        releaseChannel = updates::installedChannel();
        startUpdateCheck();
        waitPhase();
        ensure(text(updateText).find(L"already installed") != std::wstring::npos &&
                   !IsWindowEnabled(installUpdateButton),
               "Current channel status is unclear");
        queryMode = 0;
        std::cout << "PASS: rapid selection, stale completion, connection failure and "
                     "current-version status\n";
        ownership(L"");
        available(updates::Channel::Experimental);
        downloadMode = 3;
        downloadStarted = false;
        beginUpdateInstall();
        until([] { return bool(downloadStarted); }, "Download did not start");
        auto cancelled = updateJob;
        SendMessageW(updateWindow, WM_CLOSE, 0, 0);
        waitPhase();
        dialog();
        refreshUpdateUI();
        ensure(IsWindowEnabled(installUpdateButton), "Cancelled download cannot retry");
        auto cancelledDirectory = cancelled->directory;
        downloadMode = 1;
        beginUpdateInstall();
        waitPhase();
        std::weak_ptr<UpdateJob> cancelledWeak = cancelled;
        cancelled.reset();
        until([&] { return cancelledWeak.expired(); },
              "Cancelled worker did not release its staging");
        ensure(!fs::exists(cancelledDirectory) && IsWindowEnabled(installUpdateButton),
               "Retry leaked canceled download or remained disabled");
        downloadMode = 2;
        beginUpdateInstall();
        waitPhase();
        ensure(updateJob->phase == UpdateJob::failed && launchCount == 0,
               "Corrupt installer reached handoff");
        downloadMode = 0;
        beginUpdateInstall();
        waitPhase();
        std::ofstream(updateJob->settingsSnapshot, std::ios::app) << "tampered";
        pollUpdate();
        refreshUpdateUI();
        ensure(updateJob->phase == UpdateJob::failed && launchCount == 0,
               "Changed settings snapshot reached setup");
        beginUpdateInstall();
        waitPhase();
        launchSucceeds = false;
        pollUpdate();
        refreshUpdateUI();
        ensure(updateJob->phase == UpdateJob::failed && IsWindowEnabled(installUpdateButton),
               "Windows handoff failure cannot retry");
        launchSucceeds = true;
        std::cout << "PASS: cancellation/cleanup, interrupted/corrupt download, snapshot tampering "
                     "and handoff failure\n";
        auto older = updateJob->release;
        updateJob->release.tag = "v0.6.5-experimental.1";
        ensure(!supportsChoicesImport(updateJob->release) &&
                   updateInstallArguments(*updateJob, 123).empty(),
               "Older setup received unsupported settings-import arguments");
        refreshUpdateUI();
        ensure(text(updateText).find(L"older setup") != std::wstring::npos,
               "Older setup migration limitation is hidden");
        updateJob->release = older;
        int before = launchCount;
        beginUpdateInstall();
        waitPhase();
        pollUpdate();
        ensure(launchCount == before + 1 && updateJob->handedOff &&
                   launchedArguments.find(L"/IMPORTSETTINGS=\"") == 0 &&
                   launchedArguments.find(L"/AUTOUPDATE") == std::wstring::npos &&
                   launchedArguments.find(L"/D=") == std::wstring::npos,
               "Portable switch did not hand off to the interactive setup wizard");
        dialog();
        ownership(L"XPFSR24-community-alpha");
        available(updates::Channel::Normal);
        beginUpdateInstall();
        waitPhase();
        ownership(L"changed-marker");
        before = launchCount;
        pollUpdate();
        ensure(updateJob->phase == UpdateJob::failed && launchCount == before,
               "Changed installation ownership was ignored");
        ownership(L"XPFSR24-community-alpha");
        beginUpdateInstall();
        waitPhase();
        pollUpdate();
        ensure(updateJob->handedOff && launchedArguments.find(L"/S /AUTOUPDATE=1 /PARENT=") == 0 &&
                   launchedArguments.find(L" /D=" + appDirectory.wstring()) != std::wstring::npos &&
                   launchedArguments.find(L"IMPORTSETTINGS") == std::wstring::npos,
               "Owned switch did not use the recognized automatic-update route");
        std::cout << "PASS: verified wizard/automatic handoffs, saved-choice snapshot and "
                     "ownership recheck\n";
        until([] { return activeQueries == 0; }, "Background queries remain");
        DestroyWindow(window);
        theme::shutdown();
        CoUninitialize();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
