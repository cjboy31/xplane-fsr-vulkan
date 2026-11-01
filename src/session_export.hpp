// SPDX-License-Identifier: MIT
#pragma once
#include "deferred_graphics.hpp"
#include "managed_input.hpp"
#include <functional>
namespace app {
inline auto userLogDirectory(const std::filesystem::path& sim){return sim/L"Output/FSR-Logs";}
inline std::string matchingLog(const std::filesystem::path& path,const std::string& token){
    std::error_code ec;const auto state=std::filesystem::symlink_status(path,ec);
    if(ec||!std::filesystem::is_regular_file(state))return {};
    std::ifstream in(path,std::ios::binary);std::string line;std::getline(in,line);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="XPFSR24 session="+token)return {};
    std::string version,started;std::getline(in,version);std::getline(in,started);
    const auto prefix=line+"\n"+version+"\n"+started+"\n";
    auto tail=session::logTail(path,token,20*1024*1024);
    return tail.rfind(line+"\n",0)==0?tail:prefix+tail;
}
inline std::string logStarted(const std::string& text){const auto n=text.find("\nstarted_utc=");return n==std::string::npos?std::string{}:text.substr(n+13,24);}
inline void requireLogTarget(const std::filesystem::path& path){
    std::error_code ec;auto state=std::filesystem::symlink_status(path,ec);
    if(state.type()==std::filesystem::file_type::not_found)return;
    graphics::require(!ec&&std::filesystem::is_regular_file(state),"Log destination is redirected or unreadable");
    std::ifstream in(path);std::string line;std::getline(in,line);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    graphics::require(line.rfind("XPFSR24 session=",0)==0&&line.size()==48,"Log destination contains an unrecognized file; it was kept");
}
inline bool exportSessionLog(const std::filesystem::path& root,const std::filesystem::path& sim,
                             const std::wstring& token,DWORD exitCode){
    graphics::require(validSessionToken(token),"Invalid log session token");
    NamedLock snapshotLock(lockName(std::filesystem::absolute(root/L"logs").lexically_normal().wstring())+L"-log-snapshot",2000);
    const std::string id(token.begin(),token.end());
    auto text=matchingLog(root/L"logs/renderer.log",id);
    if(text.empty())text=matchingLog(root/L"logs/renderer.log.previous",id);
    if(text.empty())return false; // Never label another session's log as current.
    const auto folder=userLogDirectory(sim);graphics::require(!std::filesystem::is_symlink(folder),"Log folder is redirected");std::filesystem::create_directories(folder);
    // Keep two bounded session files. Other files in this folder are untouched.
    const auto latest=folder/L"renderer.log";
    requireLogTarget(latest);requireLogTarget(folder/L"renderer.previous.log");
    if(std::filesystem::exists(latest)){
        const auto previous=graphics::read(latest);
        if(!logStarted(previous).empty()&&!logStarted(text).empty()&&logStarted(previous)>logStarted(text))return false;
    }
    if(std::filesystem::exists(latest)&&matchingLog(latest,id).empty())
        std::filesystem::copy_file(latest,folder/L"renderer.previous.log",std::filesystem::copy_options::overwrite_existing);
    graphics::atomicWrite(latest,text+"\nExported UTC="+session::utc()+" process_exit="+
        (exitCode==STILL_ACTIVE?std::string("still running"):std::to_string(exitCode))+"\n");
    if(!std::filesystem::exists(folder/L"READ-ME.txt"))session::write(folder/L"READ-ME.txt","FSR renderer logs\r\n\r\nrenderer.log is the most recently exported session. renderer.previous.log is the previous session.\r\nA copy is saved when X-Plane exits, including an unexpected exit. Use Open log folder in the FSR panel for a snapshot during a flight.\r\nOnly the mod's renderer log is copied. No model or settings files are included.\r\n");
    return true;
}
inline void finishSession(const std::filesystem::path& root,DWORD pid,const std::wstring& token,const std::function<void()>& afterExport={}){
    graphics::require(pid&&pid!=GetCurrentProcessId()&&validSessionToken(token),"Invalid log handoff");
    NamedLock monitor(lockName(std::filesystem::absolute(root).lexically_normal().wstring())+L"-log-monitor-"+std::to_wstring(pid)+token,0);
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    graphics::require(process!=nullptr,"Simulator session ended before log monitoring started");
    struct Close{HANDLE h;~Close(){CloseHandle(h);}}close{process};
    const auto p=launcher::load(root/L"settings.ini");
    wchar_t exe[32768]{};DWORD count=32768;
    graphics::require(QueryFullProcessImageNameW(process,0,exe,&count)&&exactSimulator(std::filesystem::path(std::wstring(exe,count)),p),"Log handoff is not the selected simulator");
    const std::string id(token.begin(),token.end());
    graphics::require(!matchingLog(root/L"logs/renderer.log",id).empty(),"Log handoff belongs to another session");
    std::filesystem::create_directories(userLogDirectory(p.simulator));
    graphics::require(WaitForSingleObject(process,INFINITE)==WAIT_OBJECT_0,"Could not wait for simulator exit");
    DWORD code{};graphics::require(GetExitCodeProcess(process,&code),"Could not read simulator exit status");
    const bool exported=exportSessionLog(root,p.simulator,token,code);
    // A crash never triggers a graphics write. The user can restart safely.
    if(code==0)try{synchronizeInput(root);}catch(const std::exception& e){session::write(root/L"logs/input-setup.txt",std::string("Input setup left unchanged: ")+e.what()+"\n");}
    if(exported&&afterExport)afterExport();
}
}
