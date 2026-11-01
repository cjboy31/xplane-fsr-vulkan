// SPDX-License-Identifier: MIT
#pragma once
#include "report_format.hpp"
#include "session_export.hpp"
#include "update_feed.hpp"
#include <winhttp.h>
#include <wincrypt.h>
#include <atomic>
namespace support_report {
inline constexpr wchar_t host[]=L"xplane-mod-log-reports.cjboy31.chatgpt.site";
inline constexpr wchar_t site[]=L"https://xplane-mod-log-reports.cjboy31.chatgpt.site/";
inline std::string hash(const std::string& text){
    BCRYPT_ALG_HANDLE a{};BCRYPT_HASH_HANDLE h{};unsigned char bytes[32]{};
    if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Report hash unavailable");
    const bool ok=BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)>=0&&BCryptHashData(h,(PUCHAR)text.data(),ULONG(text.size()),0)>=0&&BCryptFinishHash(h,bytes,32,0)>=0;
    if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);if(!ok)throw std::runtime_error("Report hash failed");
    std::string out;const char* hex="0123456789abcdef";for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
struct Internet{HINTERNET h{};explicit Internet(HINTERNET handle):h(handle){if(!h)throw std::runtime_error("Reporting connection could not open");}~Internet(){WinHttpCloseHandle(h);}};
struct HttpError:std::runtime_error{DWORD status;HttpError(DWORD code,const std::string& text):std::runtime_error(text),status(code){}};
inline updates::Json request(const wchar_t* path,const std::string& data={},const std::string& token={}){
    if(!token.empty()&&!hexToken(token))throw std::runtime_error("Invalid saved reporting connection");
    Internet session(WinHttpOpen(L"XPlane-Renderer-Reports/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session.h,5000,5000,10000,15000);Internet connection(WinHttpConnect(session.h,host,443,0));
    Internet message(WinHttpOpenRequest(connection.h,data.empty()?L"GET":L"POST",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirects=WINHTTP_DISABLE_REDIRECTS,logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    graphics::require(WinHttpSetOption(message.h,WINHTTP_OPTION_DISABLE_FEATURE,&redirects,sizeof(redirects))&&WinHttpSetOption(message.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)),"Could not restrict reporting connection");
    auto headers=std::wstring(L"Content-Type: application/json\r\nAccept: application/json\r\n");if(!token.empty())headers+=L"Authorization: Bearer "+std::wstring(token.begin(),token.end())+L"\r\n";
    graphics::require(WinHttpSendRequest(message.h,headers.c_str(),DWORD(-1),data.empty()?WINHTTP_NO_REQUEST_DATA:(void*)data.data(),DWORD(data.size()),DWORD(data.size()),0)&&WinHttpReceiveResponse(message.h,nullptr),"Reporting could not connect. Your local log is kept.");
    DWORD code{},bytes=sizeof(code);graphics::require(WinHttpQueryHeaders(message.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&bytes,WINHTTP_NO_HEADER_INDEX),"Reporting response is unavailable");
    std::string response;for(;;){char buffer[4096];DWORD read{};if(!WinHttpReadData(message.h,buffer,sizeof(buffer),&read))throw std::runtime_error("Delivery could not be confirmed (HTTP "+std::to_string(code)+", network "+std::to_string(GetLastError())+"). Check Discord before retrying.");if(!read)break;graphics::require(response.size()+read<=32768,"Reporting reply was too large");response.append(buffer,read);}
    auto json=updates::Parser(response).parse();
    if(code<200||code>=300){auto e=json.fields.find("error");throw HttpError(code,e!=json.fields.end()&&e->second.type==updates::Json::string?e->second.str():"Reporting is not available. Your local log is kept.");}
    return json;
}
inline std::filesystem::path folder(const std::filesystem::path& root){auto p=root/L"logs/reports";graphics::require(!std::filesystem::is_symlink(p),"Report folder is redirected");std::filesystem::create_directories(p);return p;}
inline std::filesystem::path sessionPath(const std::filesystem::path& root,const std::wstring& token,const wchar_t* suffix){graphics::require(app::validSessionToken(token),"Invalid report session");return folder(root)/(token+suffix);}
inline void localStatus(const std::filesystem::path& root,const std::wstring& token,const std::string& text){graphics::atomicWrite(sessionPath(root,token,L".status.txt"),text);graphics::atomicWrite(folder(root)/L"last-status.txt","session="+std::string(token.begin(),token.end())+'\n'+text);}
inline std::string connection(const std::filesystem::path& root,bool refresh=false){
    const auto file=folder(root)/L"connection.dat";std::string token;
    if(std::filesystem::is_regular_file(file)&&!std::filesystem::is_symlink(file)&&std::filesystem::file_size(file)<8192){
        auto value=graphics::read(file);DATA_BLOB in{DWORD(value.size()),(BYTE*)value.data()},out{};
        if(CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)){token.assign((char*)out.pbData,out.cbData);LocalFree(out.pbData);if(hexToken(token)&&!refresh)return token;}
        graphics::require(hexToken(token),"Saved reporting connection cannot be read. It was kept. Use the report website for now.");
    }
    const auto challenge=request(L"/api/challenge").at("challenge").str();graphics::require(challenge.size()<160,"Invalid reporting challenge");
    unsigned counter=0;for(;counter<0x7fffffff;++counter)if(proof(hash(challenge+":"+std::to_string(counter))))break;
    auto value=request(L"/api/connect","{\"challenge\":"+quote(challenge)+",\"counter\":"+std::to_string(counter)+",\"consent\":true,\"policy\":"+quote(policy)+"}");
    token=value.at("token").str();graphics::require(hexToken(token),"Invalid reporting connection reply");
    DATA_BLOB in{DWORD(token.size()),(BYTE*)token.data()},out{};graphics::require(CryptProtectData(&in,L"X-Plane renderer reporting",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out),"Could not protect reporting connection");
    const std::string encrypted((char*)out.pbData,out.cbData);LocalFree(out.pbData);graphics::atomicWrite(file,encrypted);return token;
}
inline std::string currentLog(const std::filesystem::path& root,const std::wstring& token){
    const auto id=std::string(token.begin(),token.end());auto source=app::matchingLog(root/L"logs/renderer.log",id);
    if(source.empty())source=app::matchingLog(root/L"logs/renderer.log.previous",id);
    if(source.empty()){const auto sim=launcher::load(root/L"settings.ini").simulator;source=app::matchingLog(app::userLogDirectory(sim)/L"renderer.log",id);if(source.empty())source=app::matchingLog(app::userLogDirectory(sim)/L"renderer.previous.log",id);}
    graphics::require(!source.empty(),"The current renderer log is not available yet");return boundedLog(source);
}
inline uint64_t fileTime(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
inline std::string textSnapshot(const std::filesystem::path& path,uint64_t begin,uint64_t end,bool requireXPlane=false){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)return {};
    struct Close{HANDLE h;~Close(){CloseHandle(h);}}close{file};BY_HANDLE_FILE_INFORMATION before{},after{};
    if(!GetFileInformationByHandle(file,&before)||(before.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||before.nFileSizeHigh||before.nFileSizeLow>20*1024*1024||fileTime(before.ftLastWriteTime)<begin||fileTime(before.ftLastWriteTime)>end+30000000)return {};
    std::string log(before.nFileSizeLow,'\0');DWORD read{};if(!ReadFile(file,log.data(),DWORD(log.size()),&read,nullptr)||read!=log.size()||!GetFileInformationByHandle(file,&after)||after.nFileSizeLow!=before.nFileSizeLow||fileTime(after.ftLastWriteTime)!=fileTime(before.ftLastWriteTime))return {};
    if(log.rfind("\xef\xbb\xbf",0)==0)log.erase(0,3);if(requireXPlane&&log.rfind("Log.txt for X-Plane",0)!=0)return {};
    if(log.find('\0')!=std::string::npos||(!log.empty()&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,log.data(),int(log.size()),nullptr,0)))return {};
    return boundedLog(log);
}
inline std::string xplaneLog(const std::filesystem::path& root,DWORD pid){
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return {};FILETIME begin{},end{},kernel{},user{};const bool ok=GetProcessTimes(p,&begin,&end,&kernel,&user);CloseHandle(p);if(!ok)return {};
    if(!fileTime(end))GetSystemTimeAsFileTime(&end);
    const auto sim=launcher::load(root/L"settings.ini").simulator;return textSnapshot(std::filesystem::path(sim)/L"Log.txt",fileTime(begin),fileTime(end),true);
}
struct File{std::string name,text;};
inline std::string limitText(const std::string& value,size_t limit){if(value.size()<=limit)return value;while(limit&&(static_cast<unsigned char>(value[limit])&0xc0)==0x80)--limit;return value.substr(0,limit);}
inline std::string smallText(const std::string& value){return value.size()<=64*1024?value:limitText(value,60*1024)+"\n[remaining diagnostic text omitted for size]\n";}
inline std::vector<File> diagnosticFiles(const std::filesystem::path& root,const std::wstring& token,DWORD pid){
    const auto p=launcher::load(root/L"settings.ini");std::vector<File> files;
    SYSTEM_INFO system{};GetNativeSystemInfo(&system);
    std::ostringstream settings;settings<<"FSR diagnostic settings snapshot\nThese are saved choices; renderer.log shows what actually ran.\nMod="<<updates::currentVersion<<"\nSession="<<std::string(token.begin(),token.end())<<"\nWindows processor count="<<system.dwNumberOfProcessors<<"\nFSR mode="<<p.mode<<"\nFrame generation="<<p.frameGeneration<<"\nNR="<<p.neuralRendering<<"\nInput scale="<<p.manual.scale<<"\nMSAA="<<p.manual.msaa<<"\nFXAA="<<p.manual.fxaa<<"\nOutput width="<<p.manual.outputWidth<<"\nOutput height="<<p.manual.outputHeight<<"\nCloud quality="<<p.manual.cloudQuality<<"\nNR numeric controls=";
    for(int f=0;f<xpfsr::nr::Count;++f)settings<<p.nr[xpfsr::nr::Field(f)]<<',';settings<<"\nNo model, preference files, controller bindings or installation paths included.\n";files.push_back({"diagnostics.txt",settings.str()});
    uint64_t begin=0,end=0;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);FILETIME start{},stop{},kernel{},user{};
    if(process){if(GetProcessTimes(process,&start,&stop,&kernel,&user)){begin=fileTime(start);if(!fileTime(stop))GetSystemTimeAsFileTime(&stop);end=fileTime(stop);}CloseHandle(process);}
    if(!end)return files;
    std::string startup;
    for(const auto* name:{L"integration-setup.txt",L"input-setup.txt",L"integration-repair.txt"}){
        const auto text=textSnapshot(root/L"logs"/name,0,end);const std::wstring label(name);if(!text.empty())startup+="\n--- Latest "+std::string(label.begin(),label.end())+" (may predate this flight) ---\n"+text;
    }
    if(!startup.empty())files.push_back({"startup-errors.txt",smallText(startup)});
    const auto sim=std::filesystem::path(p.simulator);const auto crash=sim/L"Output/crash_reports";const auto attributes=GetFileAttributesW(crash.c_str());
    try{if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&std::filesystem::is_directory(crash)){
        std::string text;unsigned count=0;
        for(const auto& entry:std::filesystem::directory_iterator(crash)){
            if(count>=3)break;const auto extension=entry.path().extension().wstring();if(extension!=L".txt"&&extension!=L".json"&&extension!=L".rpt")continue;
            const auto snapshot=textSnapshot(entry.path(),begin,end);if(snapshot.empty())continue;text+="\n--- Same-session crash text "+std::to_string(++count)+" ---\n"+snapshot;
        }
        if(!text.empty())files.push_back({"crash-summary.txt",smallText(text)});
    }}catch(...){/* Optional crash text never prevents the main report. */}
    const auto aa=sim/L"Output/preferences/NativeAA.ini";
    try{if(std::filesystem::is_regular_file(aa)&&!std::filesystem::is_symlink(aa)&&std::filesystem::file_size(aa)<=4096){
        const std::vector<std::string> allowed={"enabled","method","quality","strength","subpixel","taa_history","motion_refinement","shimmer","motion_debug","spatial_strength"};
        std::istringstream in(graphics::read(aa));std::string line,selected="Native AA saved numeric controls (not proof it ran this session)\n";
        while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();const auto at=line.find('=');if(at==std::string::npos)continue;const auto key=line.substr(0,at),value=line.substr(at+1);if(std::find(allowed.begin(),allowed.end(),key)!=allowed.end()&&!value.empty()&&value.size()<=3&&value.find_first_not_of("0123456789")==std::string::npos)selected+=key+'='+value+'\n';}
        files.push_back({"native-aa-settings.txt",selected});
    }}catch(...){/* Optional companion preferences are numeric-only and may be absent. */}
    for(auto& file:files)file.text=smallText(redact(file.text));return files;
}
inline std::string fileJson(const std::vector<File>& files){std::string out="[";for(const auto& f:files){if(out.size()>1)out+=',';out+="{\"name\":"+quote(f.name)+",\"text\":"+quote(redact(f.text))+"}";}return out+']';}
inline std::filesystem::path saveBundle(const std::filesystem::path& root,const std::wstring& token,const std::string& renderer,const std::string& xp,const std::vector<File>& files){
    graphics::require(app::validSessionToken(token),"Invalid report session");const auto sim=launcher::load(root/L"settings.ini").simulator;
    const auto parent=app::userLogDirectory(sim)/L"reports";graphics::require(!std::filesystem::is_symlink(parent),"Report export folder is redirected");std::filesystem::create_directories(parent);
    const auto report=parent/(token+L"-"+std::to_wstring(GetTickCount64()));graphics::require(std::filesystem::create_directory(report),"A report copy already exists; no file was replaced");
    graphics::write(report/L"renderer.log",renderer);if(!xp.empty())graphics::write(report/L"X-Plane-Log.txt",xp);
    for(const auto& f:files){graphics::require(f.name=="diagnostics.txt"||f.name=="startup-errors.txt"||f.name=="crash-summary.txt"||f.name=="native-aa-settings.txt","Unknown report file");graphics::write(report/std::filesystem::path(f.name),redact(f.text));}
    graphics::write(report/L"READ-ME.txt","Filtered diagnostic copy for this session. Review every file before sharing. Filtering cannot find all private details. No model, DLL, memory dump or original preference files are included. Original logs are unchanged.\n");return report;
}
inline void send(const std::filesystem::path& root,const std::wstring& token,const std::string& log,const std::string& destination,const std::string& summary,const std::string& xplane={},const std::vector<File>& files={}){
    graphics::require(target(destination)&&summary.size()<=1000,"Invalid reporting choice");
    saveBundle(root,token,log,xplane,files);
    auto result=request(L"/api/config");graphics::require(result.at("ready").yes(),"Reporting is not connected yet. Your local log is kept.");
    auto bearer=connection(root);
    const auto payload="{\"plugin\":\"fsr\",\"target\":"+quote(destination)+",\"version\":"+quote(updates::currentVersion)+",\"summary\":"+quote(redact(summary))+",\"log\":"+quote(log)+",\"xplaneLog\":"+(xplane.empty()?std::string("null"):quote(xplane))+",\"files\":"+fileJson(files)+",\"consent\":true,\"policy\":"+quote(policy)+"}";
    localStatus(root,token,"Sending. If delivery is interrupted, check Discord before retrying.");
    updates::Json reply;try{reply=request(L"/api/reports",payload,bearer);}catch(const HttpError& e){if(e.status!=401)throw;bearer=connection(root,true);reply=request(L"/api/reports",payload,bearer);}
    graphics::require(reply.at("sent").yes(),"Delivery not confirmed");const auto link=reply.at("permalink").str();graphics::require(receipt(link,destination),"Report receipt has the wrong destination");
    localStatus(root,token,"Sent to Discord.\n"+link);
}
inline void queue(const std::filesystem::path& root,const std::wstring& token,const std::string& destination,const std::string& summary,bool includeXPlane=false){
    graphics::require(target(destination)&&summary.size()<=1000,"Invalid reporting choice");
    graphics::atomicWrite(sessionPath(root,token,L".queue.json"),"{\"policy\":"+quote(policy)+",\"consent\":true,\"includeXPlane\":"+(includeXPlane?"true":"false")+",\"target\":"+quote(destination)+",\"summary\":"+quote(redact(summary))+"}");
    localStatus(root,token,std::string("Report queued for this session to ")+(destination=="bugs"?"#bug-reports":"#render-logs")+(includeXPlane?" with available diagnostics. ":" (renderer log only). ")+"You can cancel before exit.");
}
inline void cancel(const std::filesystem::path& root,const std::wstring& token){auto path=sessionPath(root,token,L".queue.json");if(std::filesystem::is_regular_file(path)&&!std::filesystem::is_symlink(path))std::filesystem::remove(path);localStatus(root,token,"Automatic reporting is Off. No queued log will be sent.");}
using Sender=std::function<void(const std::string&,const std::string&,const std::string&,const std::string&,const std::vector<File>&)>;
inline void sendQueued(const std::filesystem::path& root,const std::wstring& token,DWORD pid,const Sender& submit={}){
    const auto path=sessionPath(root,token,L".queue.json");
    if(!std::filesystem::is_regular_file(path)||std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>4096)return;
    try{const auto text=graphics::read(path);auto data=updates::Parser(text).parse();graphics::require(data.at("policy").str()==policy&&data.at("consent").yes(),"No valid report consent");
        const auto attempted=sessionPath(root,token,L".attempted.json");graphics::require(!std::filesystem::exists(attempted),"This session's automatic report was already attempted");
        std::filesystem::rename(path,attempted);const bool include=data.at("includeXPlane").yes();const auto xp=include?xplaneLog(root,pid):std::string{};
        const auto renderer=currentLog(root,token),target=data.at("target").str(),summary=include&&xp.empty()?limitText(data.at("summary").str(),900)+"\nX-Plane Log.txt omitted: its session could not be verified.":data.at("summary").str();const auto files=include?diagnosticFiles(root,token,pid):std::vector<File>{};
        if(submit)submit(renderer,target,summary,xp,files);else send(root,token,renderer,target,summary,xp,files);
    }catch(const std::exception& e){localStatus(root,token,std::string("Report not confirmed. Your local log is kept. ")+e.what());}
}
}
