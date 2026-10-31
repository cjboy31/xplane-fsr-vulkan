// SPDX-License-Identifier: MIT
#pragma once
#include "nr_model_import.hpp"
#include "session_profile.hpp"
#include <commdlg.h>
namespace app {
inline void importNRInSim(const std::filesystem::path &root, DWORD pid,
                          const std::wstring &sessionToken, const std::wstring &job) {
    using namespace xpfsr::nr;
    if (!pid || pid == GetCurrentProcessId() || !validImportToken(sessionToken) || !validImportToken(job))
        throw std::runtime_error("Invalid model import session");
    struct Process { HANDLE handle{}; ~Process(){if(handle)CloseHandle(handle);} } sim;
    sim.handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t exe[32768]{}; DWORD count = 32768;
    if (!sim.handle || !QueryFullProcessImageNameW(sim.handle, 0, exe, &count) ||
        !exactSimulator(std::filesystem::path(std::wstring(exe, count)), launcher::load(root/L"settings.ini")))
        throw std::runtime_error("Model import is not linked to the selected simulator");
    wchar_t inherited[128]{};
    if (!GetEnvironmentVariableW(L"XPFSR24_SESSION", inherited, 128))
        GetEnvironmentVariableW(L"XPFSR24_CONFIG_SESSION", inherited, 128);
    if (sessionToken != inherited) throw std::runtime_error("Model import session changed; open it again from the in-sim panel");
    try {
        // A separate helper owns the native file picker and expensive extraction;
        // no dialog, hash, DLL loading or extraction runs on X-Plane's draw thread.
        writeModelImportProgress(root, job, "selecting", "Choose your own nvngx_dlssnr.dll (310.9.16 or 310.8.0). Cancel leaves your settings unchanged.");
        wchar_t file[32768]{}; OPENFILENAMEW picker{sizeof(picker)};
        HWND foreground = GetForegroundWindow(); DWORD foregroundPid{};
        GetWindowThreadProcessId(foreground, &foregroundPid);
        picker.hwndOwner = foregroundPid == pid ? foreground : nullptr;
        picker.lpstrFile = file; picker.nMaxFile = 32768;
        picker.lpstrTitle = L"Import NR model - select your own nvngx_dlssnr.dll (310.9.16 or 310.8.0)";
        picker.lpstrFilter = L"NVIDIA neural-rendering model DLL\0nvngx_dlssnr*.dll\0DLL files\0*.dll\0\0";
        picker.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
        if (!GetOpenFileNameW(&picker)) {
            if (CommDlgExtendedError()) throw std::runtime_error("The model file selector could not open");
            writeModelImportProgress(root, job, "cancelled", "Import cancelled. Model files and selections are unchanged."); return;
        }
        if (WaitForSingleObject(sim.handle, 0) != WAIT_TIMEOUT)
            throw std::runtime_error("The simulator session ended. Start the import again in a current session");
        const auto importer = root/L"nr/ImportNRModel.exe", staging = root/L"nr"/(L"model-import-"+job);
        if (!std::filesystem::is_regular_file(importer)) throw std::runtime_error("NR importer is missing; reinstall the complete package");
        if (!std::filesystem::create_directory(staging)) throw std::runtime_error("A model import staging folder already exists; retry import");
        writeModelImportProgress(root, job, "extracting", "Reading and validating the DLL; extracting supported models. The DLL is never executed. FSR continues.");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        auto log = CreateFileW((staging/L"import.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create the local import diagnostic");
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=startup.hStdError=log; startup.hStdInput=nullptr;
        PROCESS_INFORMATION process{};
        auto command=L"\""+importer.wstring()+L"\" \""+file+L"\" \""+staging.wstring()+L"\"";
        bool started=CreateProcessW(importer.c_str(),command.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,root.c_str(),&startup,&process)!=FALSE;
        CloseHandle(log);
        if (!started) throw std::runtime_error("Could not start the NR importer");
        CloseHandle(process.hThread); Process child; child.handle=process.hProcess;
        if (WaitForSingleObject(child.handle, INFINITE)!=WAIT_OBJECT_0) throw std::runtime_error("Could not wait for model extraction");
        DWORD exitCode=1; GetExitCodeProcess(child.handle,&exitCode);
        if(exitCode) throw std::runtime_error("DLL not supported or model extraction failed. Select nvngx_dlssnr.dll 310.9.16 or 310.8.0; see the local import diagnostic for details");
        writeModelImportProgress(root,job,"verifying","Verifying model hashes before installation. Existing model files remain protected.");
        int installed=installExtractedModels(root,staging,false);
        writeModelImportProgress(root,job,"success","Verified "+std::to_string(installed)+" model(s). Turn NR Off, then On to prepare it live. No simulator restart is needed for a first import.");
    } catch(const std::exception &e) {
        writeModelImportProgress(root,job,"error",e.what()); throw;
    }
}
} // namespace app
