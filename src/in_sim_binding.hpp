// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
namespace insim {
class ConfigurationSession {
    HANDLE owner{};std::string token;
public:
    ConfigurationSession()=default;ConfigurationSession(const ConfigurationSession&)=delete;
    ~ConfigurationSession(){close();}
    void close(){if(owner)CloseHandle(owner);owner=nullptr;token.clear();}
    std::string open(const std::filesystem::path &root){
        if(!token.empty())return token;
        if(!owner){auto name=app::lockName((root/L"logs/active-session").wstring())+L"-presence";
            auto handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,1,name.c_str());bool existed=GetLastError()==ERROR_ALREADY_EXISTS;
            if(handle&&!existed)owner=handle;else if(handle)CloseHandle(handle);}
        token=owner?session::prepare(root/L"logs","activation=plugin-only renderer_loaded=0"):session::id();
        if(owner)session::write(root/L"logs/renderer.log",session::header(token,"plugin-only")+"Renderer not loaded during Vulkan startup. Saved profile recovered for this selected simulator. Restart is required to activate rendering.\n");
        return token;
    }
};
struct SettingsBinding { std::filesystem::path file; bool recovered{}; std::string reason; };
inline SettingsBinding resolveSettings(const std::filesystem::path &executable,
    const std::filesystem::path &environmentFile, const std::filesystem::path &runtime,
    const std::filesystem::path &registeredManifest) {
    auto valid=[&](const std::filesystem::path &file){
        std::error_code ec;
        return file.is_absolute()&&file.filename()==L"settings.ini"&&
            std::filesystem::is_regular_file(file,ec)&&!ec&&app::exactSimulator(executable,launcher::load(file));
    };
    if(valid(environmentFile))return {std::filesystem::weakly_canonical(environmentFile),false};
    // Older optional desktop versions do not publish XPFSR24_SETTINGS. Recover
    // their own profile from the process-local runtime path, never a disk scan.
    if(runtime.is_absolute()&&valid(runtime.parent_path()/L"settings.ini"))
        return {std::filesystem::weakly_canonical(runtime.parent_path()/L"settings.ini"),true};
    if(registeredManifest.empty())return {{},false,"No installation registration is visible to this simulator session"};
    if(registeredManifest.is_absolute()&&registeredManifest.filename()==L"XPFSR24Implicit.json"&&
        registeredManifest.parent_path().filename()==L"auto"){
        auto root=registeredManifest.parent_path().parent_path(),file=root/L"settings.ini";
        std::error_code ec;
        if(!std::filesystem::is_regular_file(registeredManifest,ec)||ec)return {{},false,"The registered Vulkan manifest is missing or inaccessible"};
        if(!std::filesystem::is_regular_file(file,ec)||ec)return {{},false,"The registered settings file is missing or inaccessible"};
        if(!valid(file))return {{},false,"The registered profile does not match the current simulator executable"};
        if(launcher::readSetting(file,L"InSim",L"Enabled",L"0")!=L"1")return {{},false,"In-sim integration is disabled in the registered installation"};
        return {std::filesystem::weakly_canonical(file),true};
    }
    return {{},false,"The registered installation manifest path is invalid"};
}
} // namespace insim
