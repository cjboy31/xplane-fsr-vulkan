// SPDX-License-Identifier: MIT
#pragma once
// Protocol v1: shared verbatim with XPFSR24. One startup decision per process;
// changing the per-simulator preference never hot-unloads an in-flight layer.
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace xp_renderer {
enum class Choice { automatic, native_aa, fsr, off };
inline const char* key(Choice c){switch(c){case Choice::automatic:return "auto";case Choice::native_aa:return "native_aa";case Choice::fsr:return "fsr";default:return "off";}}
inline const char* label(Choice c){switch(c){case Choice::automatic:return "Automatic";case Choice::native_aa:return "Native AA";case Choice::fsr:return "FSR / NR / frame generation";default:return "Original simulator";}}
inline Choice parse(const std::string& s){if(s=="auto")return Choice::automatic;if(s=="native_aa")return Choice::native_aa;if(s=="fsr")return Choice::fsr;if(s=="off")return Choice::off;throw std::runtime_error("Invalid renderer choice");}
inline std::string environment(const char* name){
#ifdef _WIN32
    char value[128]{};const auto n=GetEnvironmentVariableA(name,value,sizeof(value));return n&&n<sizeof(value)?std::string(value,n):std::string{};
#else
    const char* value=std::getenv(name);return value?value:"";
#endif
}
inline bool set(const char* name,const std::string& value){
#ifdef _WIN32
    return SetEnvironmentVariableA(name,value.empty()?nullptr:value.c_str())!=0;
#else
    return value.empty()?unsetenv(name)==0:setenv(name,value.c_str(),1)==0;
#endif
}
inline std::string pid(){
#ifdef _WIN32
    return std::to_string(GetCurrentProcessId());
#else
    return std::to_string(getpid());
#endif
}
inline auto file(const std::filesystem::path& root){return root/"Output/preferences/XP-Renderer-Selection.ini";}
inline Choice read(const std::filesystem::path& root){
    const auto path=file(root);std::error_code ec;
    const auto state=std::filesystem::symlink_status(path,ec);
    if(state.type()==std::filesystem::file_type::not_found)return Choice::automatic;
    if(ec||!std::filesystem::is_regular_file(state)||std::filesystem::is_symlink(state))throw std::runtime_error("Renderer selection is unreadable or redirected");
    std::ifstream in(path,std::ios::binary);std::string data(257,'\0');in.read(data.data(),257);data.resize(std::size_t(in.gcount()));
    if(data.size()>256||in.bad())throw std::runtime_error("Renderer selection is invalid");
    std::string normalized;for(char c:data)if(c!='\r')normalized+=c;
    const std::string prefix="XPlaneRendererSelection=1\nrenderer=";
    if(normalized.rfind(prefix,0)!=0||normalized.empty()||normalized.back()!='\n')throw std::runtime_error("Renderer selection format is invalid");
    return parse(normalized.substr(prefix.size(),normalized.size()-prefix.size()-1));
}
inline bool aa_installed(const std::filesystem::path& root){std::ifstream in(root/"Resources/plugins/NativeAA/standalone.enabled");std::string line;std::getline(in,line);return line=="NativeAAStandalone=1";}
class Lock {
#ifdef _WIN32
    HANDLE handle{};
#else
    int fd=-1;
#endif
public:
    explicit Lock(const std::filesystem::path& root){
#ifdef _WIN32
        (void)root;const auto name=L"Local\\XPlane-Renderer-Selection-v1-"+std::to_wstring(GetCurrentProcessId());
        handle=CreateMutexW(nullptr,FALSE,name.c_str());
        const auto result=handle?WaitForSingleObject(handle,2000):WAIT_FAILED;
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){if(handle)CloseHandle(handle);handle=nullptr;throw std::runtime_error("Renderer selection lock unavailable");}
#else
        std::filesystem::create_directories(file(root).parent_path());
        fd=open((file(root).string()+".lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        struct stat st{};
        if(fd<0||fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||flock(fd,LOCK_EX)!=0){if(fd>=0)close(fd);fd=-1;throw std::runtime_error("Renderer selection lock unavailable");}
#endif
    }
    Lock(const Lock&)=delete;Lock& operator=(const Lock&)=delete;
    ~Lock(){
#ifdef _WIN32
        if(handle){ReleaseMutex(handle);CloseHandle(handle);}
#else
        if(fd>=0){flock(fd,LOCK_UN);close(fd);}
#endif
    }
};
inline void save(const std::filesystem::path& root,Choice choice){
    if(!root.is_absolute())throw std::runtime_error("Select an absolute simulator folder");
    Lock lock(root);const auto path=file(root);std::filesystem::create_directories(path.parent_path());
    std::error_code ec;const auto st=std::filesystem::symlink_status(path,ec);
    if(st.type()!=std::filesystem::file_type::not_found&&(ec||!std::filesystem::is_regular_file(st)||std::filesystem::is_symlink(st)))throw std::runtime_error("Renderer selection is redirected");
    const auto data=std::string("XPlaneRendererSelection=1\nrenderer=")+key(choice)+"\n";
    auto temporary=path;temporary+=".pending-"+pid();
#ifdef _WIN32
    auto h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create renderer selection; check write access");
    DWORD written{};const bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);
    if(!ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot save renderer selection");}
#else
    const int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)throw std::runtime_error("Cannot create renderer selection; check write access");
    const bool ok=write(fd,data.data(),data.size())==ssize_t(data.size())&&fsync(fd)==0;close(fd);
    if(!ok||rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());throw std::runtime_error("Cannot save renderer selection");}
#endif
}
struct Decision {bool allowed{};Choice selected{Choice::off};std::string reason;};
inline Decision claim(const std::filesystem::path& root,Choice request,bool explicit_request=false,bool legacy_fsr=false){
    if(request!=Choice::native_aa&&request!=Choice::fsr)return {false,Choice::off,"Only a renderer may claim processing"};
    if(!root.is_absolute())return {false,Choice::off,"Simulator folder must be absolute"};
    try{
        Lock lock(root);
        if(environment("XP_RENDERER_SESSION_PID")!=pid()){
            if(!set("XP_RENDERER_SESSION_CHOICE",{})||!set("XP_RENDERER_OWNER",{})||!set("XP_RENDERER_SESSION_PID",pid()))throw std::runtime_error("Cannot initialize renderer session identity");
        }
        auto snapshot=environment("XP_RENDERER_SESSION_CHOICE");Choice selected;
        if(snapshot.empty()){
            auto override=environment("XP_RENDERER_REQUEST");
            selected=!override.empty()?parse(override):explicit_request?request:read(root);
            if(selected==Choice::automatic)selected=aa_installed(root)?Choice::native_aa:Choice::fsr;
            if(!set("XP_RENDERER_SESSION_CHOICE",key(selected)))throw std::runtime_error("Cannot latch renderer selection");
        }else selected=parse(snapshot);
        auto owner=environment("XP_RENDERER_OWNER");
        // Respect older FSR releases which already prepared an automatic session.
        // Never activate AA over an existing renderer just to satisfy a preference.
        if(owner.empty()&&legacy_fsr){owner="fsr";if(!set("XP_RENDERER_OWNER",owner))throw std::runtime_error("Cannot record existing FSR owner");}
        if(!owner.empty()&&owner!=key(request))return {false,selected,std::string(owner=="fsr"?"FSR":"Native AA")+" owns this session. Change Renderer and restart to switch."};
        if(selected!=request){
            if(selected==Choice::off&&!set("DISABLE_XPFSR24","1"))throw std::runtime_error("Cannot bypass FSR for an original-simulator session");
            return {false,selected,std::string(label(selected))+" selected for this session. This renderer is inactive."};
        }
        if(!set("XP_RENDERER_OWNER",key(request)))throw std::runtime_error("Cannot claim the renderer session");
        if(request==Choice::native_aa&&!set("DISABLE_XPFSR24","1"))throw std::runtime_error("Cannot bypass FSR for Native AA");
        return {true,selected,{}};
    }catch(const std::exception& e){return {false,Choice::off,e.what()};}
}
} // namespace xp_renderer
