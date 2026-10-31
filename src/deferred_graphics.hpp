// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
#include "settings_transaction.hpp"
#include "managed_input.hpp"
namespace app {
inline bool validSessionToken(const std::wstring &s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
inline bool applyQueuedGraphics(const std::filesystem::path &root,const std::wstring &token){
    graphics::require(validSessionToken(token),"Invalid graphics session token");
    const auto file=root/L"settings.ini";
    if(launcher::readSetting(file,L"InSim",L"GraphicsQueued")!=token)return false;
    auto before=launcher::load(file);if(!before.manualPending)return false;
    auto options=before.presetId.empty()?before.manual:graphics::presetById(before.presetId).settings;options.keepUISize=before.keepSimUISize;
    options=compatibleInput(before,xp_renderer::visibleChoice(before.simulator),options);
    auto backup=graphics::apply(before.simulator,root/L"graphics-backups",options);
    auto after=before;after.manual=options;after.manualApplied=true;after.manualPending=false;
    launcher::transactChoices(file,before,after);
    if(!backup.empty())graphics::require(launcher::writeSetting(L"Graphics",L"LastBackup",backup.c_str(),file.c_str()),"Graphics applied but backup location could not be saved");
    graphics::require(launcher::writeSetting(L"InSim",L"GraphicsQueued",L"",file.c_str()),"Graphics applied but queued request could not be cleared");
    return true;
}
inline void waitAndApplyGraphics(const std::filesystem::path &root,DWORD pid,const std::wstring &token){
    graphics::require(pid&&pid!=GetCurrentProcessId()&&validSessionToken(token),"Invalid simulator graphics handoff");
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    graphics::require(process!=nullptr,"The selected simulator session has already closed; queue graphics again in-sim");
    struct Cleanup{HANDLE h;~Cleanup(){CloseHandle(h);}}cleanup{process};
    wchar_t executable[32768]{};DWORD count=32768;
    graphics::require(QueryFullProcessImageNameW(process,0,executable,&count)&&exactSimulator(std::filesystem::path(std::wstring(executable,count)),launcher::load(root/L"settings.ini")),"Graphics handoff is not the selected simulator");
    graphics::require(WaitForSingleObject(process,INFINITE)==WAIT_OBJECT_0,"Could not wait for simulator exit");
    DWORD code{};graphics::require(GetExitCodeProcess(process,&code)&&code==0,"Simulator did not exit normally. Graphics were left unchanged; queue the change again");
    if(applyQueuedGraphics(root,token))session::write(root/L"logs/integration-task.txt","Queued graphics applied after normal simulator exit.\n");
}
} // namespace app
