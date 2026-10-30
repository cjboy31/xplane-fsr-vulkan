// SPDX-License-Identifier: MIT
// Models are local user assets. The selected NVIDIA DLL is read, never loaded.
#include "nr_models.hpp"
static HWND nrWindow{}, nrText{}, nrImportButton{}, nrCheckButton{};
static HANDLE nrJob{};
static bool nrImporting{}, nrModelsKnown{}, nrModelsValid{};
static fs::path nrStaging;
static bool nrJobRunning() { return nrJob && WaitForSingleObject(nrJob, 0) == WAIT_TIMEOUT; }
static bool nrModelReady() {
    if (nrModelsKnown)
        return nrModelsValid;
    nrModelsKnown = true;
    nrModelsValid = false;
    try {
        const auto data = appDirectory / L"nr/dlssnr-amd";

        if (!fs::is_regular_file(appDirectory / L"nr/XPFSRNeuralRendering.dll"))
            return false;
        for (auto name : {L"dlssnr-small.bin", L"dlssnr.bin"}) {
            if (!fs::is_regular_file(data / name))
                continue;
            if (!xpfsr::nr::acceptedModel(!wcscmp(name, L"dlssnr-small.bin"),
                                          fs::file_size(data / name), hash(data / name))) {
                nrModelsValid = false;
                return false;
            }
            nrModelsValid = true;
        }
    } catch (...) {
        nrModelsValid = false;
    }
    return nrModelsValid;
}
static std::wstring nrLiveStatus() {
    if(!running())return L"NR settings saved. Launch with FSR to adjust them live.";
    if (launchedNative || !preferences.neuralRendering)
        return L"NR Off";
    std::ifstream in(appDirectory / L"logs/renderer-nr-status.txt");
    uint64_t pid{}, tick{}, recorded{};
    std::string token, reason;
    if (in >> pid >> tick >> recorded) {
        std::getline(in, token);
        std::getline(in, token);
        std::getline(in, reason);
        const auto now = GetTickCount64();
        if (pid == childId && token == activeSessionToken && tick <= now && now - tick < 3000)
            return wide(reason.c_str()) + L"\r\nNR recordings: " + std::to_wstring(recorded);
    }
    return L"NR waiting for current-session renderer status";
}
static std::wstring nrSetupText() {
    return std::wstring(nrModelReady() ? L"MODEL IMPORT VERIFIED\r\n" : L"MODEL REQUIRED\r\n") +
           L"Import your own nvngx_dlssnr.dll 310.9.16 (small + original models) or 310.8.0 "
           L"(original). "
           L"The importer verifies the DLL and every extracted tensor against the pinned reference "
           L"hashes. "
           L"It reads the file without executing it. NVIDIA's DLL and model weights are not "
           L"included in our downloads.\r\n\r\n"
           L"FIRST TEST\r\n1. Close X-Plane. Import the model, then run NR check.\r\n"
           L"2. Choose FSR Native AA/upscaling, NR Subtle and FG Off. Launch here.\r\n"
           L"3. Start with External views only. Choose Full scene in Scene scope to include the cockpit, live. "
           L"The ground-contact safety guard still takes precedence. "
           L"Compare the same scene before increasing strength.\r\n\r\n"
           L"This is the independent DLSSNR-AMD project adapted for AMD RDNA4 FP8 hardware, not an "
           L"AMD-official feature. "
           L"It changes image detail/lighting and adds GPU cost. It does not fix frame generation. "
           L"Motion is GPU-estimated; clouds/objects are not exact engine motion.\r\n\r\n"
           L"LIVE CONTROLS\r\nOn/Off, strength, tone, structure, color, history, style and passes apply at frame boundaries. "
           L"Model and resolution budgets prepare in the background without a sim reset. Start with 50% model resolution and one pass; "
           L"use Performance (33%) to reduce network cost. Importing model files still requires X-Plane closed. "
           L"Combining NR and FG requires the explicit experimental option, with the FG provider selected before launch.\r\n\r\n"
           L"INITIAL LIMITS\r\nWindows Vulkan, one graphics queue/view. "
           L"An ASCII application path is required. Missing/unsupported inputs bypass NR with a reason. FSR retains its "
           L"original input. "
           L"The GPU check is a controlled fixture, not live-flight validation.";
}
static void nrStartJob(const fs::path &exe, const std::wstring &arguments, bool import) {
    graphics::ensureSimulatorClosed();
    ensure(!running(), "Close X-Plane before importing or checking an NR model");
    ensure(!nrJobRunning(), "Wait for the current NR task to finish");
    ensure(!aaChild || WaitForSingleObject(aaChild, 0) != WAIT_TIMEOUT,
           "Wait for the current GPU check to finish");
    if (nrJob) {
        CloseHandle(nrJob);
        nrJob = nullptr;
    }
    fs::create_directories(appDirectory / L"nr-check");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const auto output = appDirectory / L"nr-check/task.txt";
    auto file = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(file != INVALID_HANDLE_VALUE, "Cannot write NR task log");
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = file;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    auto command = quote(exe.wstring()) + L" " + arguments;
    const bool ok =
        CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, appDirectory.c_str(), &startup, &process) != FALSE;
    CloseHandle(file);
    ensure(ok, "Cannot start the NR task; reinstall the complete application");
    CloseHandle(process.hThread);
    nrJob = process.hProcess;
    nrImporting = import;
    SetWindowTextW(nrText, import ? L"Validating and extracting your model. The NVIDIA DLL is read "
                                    L"only; it is never executed."
                                  : L"Running a fenced NR GPU/model check in a separate process. "
                                    L"No simulator is started.");
    EnableWindow(nrImportButton, FALSE);
    EnableWindow(nrCheckButton, FALSE);
}
static void nrImportModel() {
    ensure(!running(), "Close X-Plane before importing the neural model");
    wchar_t file[32768]{};
    OPENFILENAMEW choice{sizeof(choice)};
    choice.hwndOwner = nrWindow;
    choice.lpstrFile = file;
    choice.nMaxFile = 32768;
    choice.lpstrTitle = L"Select your own nvngx_dlssnr.dll (310.9.16 or 310.8.0)";
    choice.lpstrFilter = L"NVIDIA NR model DLL\0nvngx_dlssnr*.dll\0All DLL files\0*.dll\0\0";
    choice.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&choice))
        return;
    nrStaging = appDirectory / L"nr" / (L"model-import-" + wide(session::id().c_str()));
    fs::create_directories(nrStaging);
    nrStartJob(appDirectory / L"nr/ImportNRModel.exe",
               quote(file) + L" " + quote(nrStaging.wstring()), true);
}
static void nrFinishJob() {
    if (!nrJob || nrJobRunning())
        return;
    DWORD code = 1;
    GetExitCodeProcess(nrJob, &code);
    CloseHandle(nrJob);
    nrJob = nullptr;
    std::wstring result;
    try {
        if (nrImporting && code == 0) {
            bool found = false;
            const auto data = appDirectory / L"nr/dlssnr-amd";
            fs::create_directories(data);
            for (auto name : {L"dlssnr-small.bin", L"dlssnr.bin"}) {
                const auto source = nrStaging / name, target = data / name;
                if (!fs::is_regular_file(source))
                    continue;
                const auto digest = hash(source);
                ensure(xpfsr::nr::acceptedModel(!wcscmp(name, L"dlssnr-small.bin"),
                                                fs::file_size(source), digest),
                       "Extracted model does not match the pinned model hash");
                found = true;
                if (fs::exists(target) && hash(target) != digest) {
                    const auto backup = data / (std::wstring(name) + L".previous-" +
                                                wide(hash(target).c_str()) + L".bin");
                    if (!fs::exists(backup))
                        fs::copy_file(target, backup);
                    ensure(hash(backup) == hash(target),
                           "Previous NR model backup verification failed");
                }
                ensure(MoveFileExW(source.c_str(), target.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE,
                       "Cannot install the validated local model");
                ensure(WritePrivateProfileStringW(L"Models", name, wide(digest.c_str()).c_str(),
                                                  (appDirectory / L"nr/USER-MODELS.ini").c_str()) !=
                           FALSE,
                       "Cannot record local model verification");
            }
            ensure(found, "The importer did not produce a supported model");
            if (fs::is_empty(nrStaging))
                fs::remove(nrStaging);
            nrModelsKnown = false;
            result = nrSetupText();
        } else {
            std::ifstream log(appDirectory / L"nr-check/task.txt");
            std::string text((std::istreambuf_iterator<char>(log)), {});
            if (text.size() > 24000)
                text = text.substr(text.size() - 24000);
            result = std::wstring(code == 0 ? L"NR CHECK FINISHED\r\n" : L"NR TASK FAILED\r\n") +
                     wide(text.c_str());
        }
    } catch (const std::exception &e) {
        result = L"NR task failed: " + wide(e.what());
    }
    if (IsWindow(nrWindow)) {
        SetWindowTextW(nrText, result.c_str());
        EnableWindow(nrImportButton, TRUE);
        EnableWindow(nrCheckButton, nrModelReady());
    }
}
#include "nr_live_ui.inc"
