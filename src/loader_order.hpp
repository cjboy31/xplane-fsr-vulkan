// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
#include "runtime_integrity.hpp"
namespace app {
// Vulkan Loader 1.4.304+ supports an exact-application layer order. Putting our
// queue reservation ahead of capture lets capture observe the expanded device
// request. Do not disable OBS globally or replace somebody else's loader file.
inline std::string loaderJsonString(const std::wstring &wide){
    int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);
    graphics::require(count>0,"Invalid loader-profile path");std::string value(count,0),out="\"";
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),value.data(),count,nullptr,nullptr);
    const char hex[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='\\'||c=='\"'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}
    return out+'\"';
}
inline std::filesystem::path loaderOrderFile(const std::filesystem::path &root){return std::filesystem::weakly_canonical(root/L"auto/loader/vk_loader_settings.json");}
inline std::string loaderOrderContents(const std::filesystem::path &root,const std::filesystem::path &exe){
    graphics::require(root.is_absolute()&&exe.is_absolute(),"Absolute loader-profile paths required");
    return "{\n \"file_format_version\":\"1.0.0\",\n \"settings\":{\n  \"app_keys\":["+loaderJsonString(std::filesystem::weakly_canonical(exe).wstring())+"],\n  \"layers\":[{\"name\":\"VK_LAYER_XPFSR24\",\"path\":"+loaderJsonString(std::filesystem::weakly_canonical(root/L"auto/XPFSR24Implicit.json").wstring())+",\"control\":\"auto\",\"treat_as_implicit_manifest\":true},{\"control\":\"unordered_layer_location\"}]\n }\n}\n";
}
inline bool loaderSupportsOrder(){
    auto loader=GetModuleHandleW(L"vulkan-1.dll");wchar_t path[32768]{};
    if(!loader||!GetModuleFileNameW(loader,path,32768))return false;
    auto version=LoadLibraryExW(L"version.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!version)return false;
    struct Release{HMODULE h;~Release(){FreeLibrary(h);}}release{version};
    auto size=reinterpret_cast<decltype(&GetFileVersionInfoSizeW)>(GetProcAddress(version,"GetFileVersionInfoSizeW"));
    auto get=reinterpret_cast<decltype(&GetFileVersionInfoW)>(GetProcAddress(version,"GetFileVersionInfoW"));
    auto query=reinterpret_cast<decltype(&VerQueryValueW)>(GetProcAddress(version,"VerQueryValueW"));
    if(!size||!get||!query)return false;DWORD unused{},bytes=size(path,&unused);if(!bytes||bytes>1024*1024)return false;
    std::vector<unsigned char> data(bytes);if(!get(path,0,bytes,data.data()))return false;
    VS_FIXEDFILEINFO *info{};UINT length{};
    if(!query(data.data(),L"\\",reinterpret_cast<void**>(&info),&length)||length<sizeof(*info)||info->dwSignature!=0xfeef04bd)return false;
    const auto major=HIWORD(info->dwFileVersionMS),minor=LOWORD(info->dwFileVersionMS),patch=HIWORD(info->dwFileVersionLS);
    return major>1||(major==1&&(minor>4||(minor==4&&patch>=304)));
}
class LoaderOrderRegistration {
    HKEY hive;std::wstring key,owner;
    static void checked(LSTATUS result){graphics::require(result==ERROR_SUCCESS,"Cannot update the application-specific Vulkan layer order");}
    static std::wstring stringValue(HKEY scope,const std::wstring &key,const wchar_t *name){
        wchar_t value[32768]{};DWORD bytes=sizeof(value);auto rc=RegGetValueW(scope,key.c_str(),name,RRF_RT_REG_SZ,nullptr,value,&bytes);
        if(rc==ERROR_FILE_NOT_FOUND||rc==ERROR_PATH_NOT_FOUND)return {};checked(rc);return value;
    }
    bool enabledValue(const std::filesystem::path &file)const{DWORD value=1,bytes=sizeof(value);return RegGetValueW(hive,key.c_str(),file.c_str(),RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS&&value==0;}
    bool competing(HKEY scope,const std::filesystem::path &ours,bool ownScope)const{
        HKEY handle{};auto rc=RegOpenKeyExW(scope,key.c_str(),0,KEY_QUERY_VALUE,&handle);
        if(rc==ERROR_FILE_NOT_FOUND||rc==ERROR_PATH_NOT_FOUND)return false;checked(rc);
        struct Close{HKEY h;~Close(){RegCloseKey(h);}}close{handle};
        for(DWORD i=0;;++i){wchar_t name[32768]{};DWORD chars=32768,type{},value{},bytes=sizeof(value);
            rc=RegEnumValueW(handle,i,name,&chars,nullptr,&type,reinterpret_cast<BYTE*>(&value),&bytes);
            if(rc==ERROR_NO_MORE_ITEMS)break;
            if(rc==ERROR_MORE_DATA)return true;checked(rc);
            if(type!=REG_DWORD||bytes!=sizeof(value)||value)continue;
            if(ownScope&&!_wcsicmp(name,ours.c_str()))continue;
            std::error_code ec;if(std::filesystem::is_regular_file(name,ec)&&!ec)return true;
        }return false;
    }
    bool competition(const std::filesystem::path &file)const{return competing(hive,file,true)||(hive==HKEY_CURRENT_USER&&key==L"SOFTWARE\\Khronos\\Vulkan\\LoaderSettings"&&competing(HKEY_LOCAL_MACHINE,file,false));}
public:
    explicit LoaderOrderRegistration(HKEY scope=HKEY_CURRENT_USER,std::wstring registry=L"SOFTWARE\\Khronos\\Vulkan\\LoaderSettings",std::wstring ownership=L"SOFTWARE\\XPFSR24\\InSim"):hive(scope),key(std::move(registry)),owner(std::move(ownership)){}
    void enable(const std::filesystem::path &root,const std::filesystem::path &exe){
        const auto file=loaderOrderFile(root);const auto text=loaderOrderContents(root,exe);
        graphics::require(!competition(file),"Another Vulkan loader profile is active. It was kept. Merge the X-Plane layer order there before using capture with frame generation");
        const auto recorded=stringValue(hive,owner,L"LoaderOrderSHA256");
        if(std::filesystem::exists(file)){
            const auto digest=fileHash(file);
            graphics::require((std::filesystem::file_size(file)<65536&&graphics::read(file)==text)||std::wstring(digest.begin(),digest.end())==recorded,"The X-Plane layer-order file was edited. It was kept; restore it before repairing setup");
        }
        std::filesystem::create_directories(file.parent_path());auto temporary=file;temporary+=L".new-"+std::to_wstring(GetCurrentProcessId());
        graphics::require(!std::filesystem::exists(temporary),"A loader-profile staging file already exists");
        struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);}}cleanup{temporary};
        graphics::write(temporary,text);graphics::require(graphics::read(temporary)==text,"Cannot verify the application-specific layer order");
        graphics::require(MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Cannot save the application-specific layer order");
        HKEY handle{};checked(RegCreateKeyExW(hive,owner.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr));
        const auto digest=fileHash(file);const std::wstring hash(digest.begin(),digest.end());
        auto rc=RegSetValueExW(handle,L"LoaderOrderSHA256",0,REG_SZ,reinterpret_cast<const BYTE*>(hash.c_str()),DWORD((hash.size()+1)*sizeof(wchar_t)));RegCloseKey(handle);checked(rc);
        checked(RegCreateKeyExW(hive,key.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr));DWORD zero=0;
        rc=RegSetValueExW(handle,file.c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&zero),sizeof(zero));RegCloseKey(handle);checked(rc);
    }
    void disable(const std::filesystem::path &root){
        const auto file=loaderOrderFile(root);HKEY handle{};auto rc=RegOpenKeyExW(hive,key.c_str(),0,KEY_SET_VALUE,&handle);
        if(rc==ERROR_SUCCESS){rc=RegDeleteValueW(handle,file.c_str());RegCloseKey(handle);if(rc!=ERROR_FILE_NOT_FOUND)checked(rc);}
        else if(rc!=ERROR_FILE_NOT_FOUND&&rc!=ERROR_PATH_NOT_FOUND)checked(rc);
        const auto recorded=stringValue(hive,owner,L"LoaderOrderSHA256");
        if(!recorded.empty()&&std::filesystem::is_regular_file(file)){
            const auto hash=fileHash(file);if(recorded==std::wstring(hash.begin(),hash.end())){std::error_code ec;std::filesystem::remove(file,ec);std::filesystem::remove(file.parent_path(),ec);}
        }
    }
    bool active(const std::filesystem::path &root,const std::filesystem::path &exe)const noexcept{try{
        const auto file=loaderOrderFile(root);return loaderSupportsOrder()&&enabledValue(file)&&!competition(file)&&std::filesystem::is_regular_file(file)&&std::filesystem::file_size(file)<65536&&graphics::read(file)==loaderOrderContents(root,exe);
    }catch(...){return false;}}
};
} // namespace app
