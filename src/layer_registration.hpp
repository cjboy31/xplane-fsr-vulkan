// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
#include "named_lock.hpp"
#include "loader_order.hpp"
#include "installation_link.hpp"
namespace app {
inline constexpr const wchar_t *implicitKey=L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers";
inline constexpr const wchar_t *integrationOwnerKey=L"SOFTWARE\\XPFSR24\\InSim";
inline std::wstring registryString(HKEY hive,const wchar_t *key,const wchar_t *value){
    wchar_t text[32768]{};DWORD bytes=sizeof(text);auto rc=RegGetValueW(hive,key,value,RRF_RT_REG_SZ,nullptr,text,&bytes);
    if(rc==ERROR_FILE_NOT_FOUND||rc==ERROR_PATH_NOT_FOUND)return {};
    graphics::require(rc==ERROR_SUCCESS&&bytes>=sizeof(wchar_t)&&bytes<=sizeof(text),"Cannot read the in-sim registration owner");
    return text;
}
// Only this installation's exact manifest value is written/removed. Other
// implicit layers and other installations' ownership records are retained.
class LayerRegistration {
    HKEY hive;std::wstring layerKey,ownerKey;
    static void checked(LSTATUS rc){graphics::require(rc==ERROR_SUCCESS,"Cannot update the per-user Vulkan registration");}
    std::filesystem::path previousOwner(const std::filesystem::path& root,bool migration){
        const auto m=manifest(root);const auto previous=registryString(hive,ownerKey.c_str(),L"Manifest");
        if(previous.empty()||!_wcsicmp(previous.c_str(),m.c_str()))return {};
        const std::filesystem::path oldManifest=previous;const auto oldRoot=oldManifest.parent_path().parent_path();
        const auto current=launcher::load(root/L"settings.ini");
        const bool known=oldManifest.is_absolute()&&oldManifest.filename()==L"XPFSR24Implicit.json"&&oldManifest.parent_path().filename()==L"auto"&&
            std::filesystem::is_regular_file(oldManifest)&&std::filesystem::is_regular_file(oldRoot/L"settings.ini")&&
            exactSimulator(std::filesystem::path(current.simulator)/L"X-Plane.exe",launcher::load(oldRoot/L"settings.ini"));
        bool verified=false;
        if(known&&migration){
            const auto newHash=fileHash(root/L"plugin/64/win.xpl");
            const auto recorded=launcher::readSetting(root/L"settings.ini",L"InSim",L"PluginSHA256");
            const auto replacement=std::string(recorded.begin(),recorded.end())==newHash?newHash:std::string{};
            const auto owner=verifiedPluginOwner(current.simulator,replacement);
            verified=!owner.empty()&&!_wcsicmp(owner.c_str(),std::filesystem::weakly_canonical(oldRoot).c_str());
            if(owner.empty())verified=verifiedPreviousPlugin(oldRoot,current.simulator,replacement);
        }
        graphics::require(known&&(verified||launcher::readSetting(oldRoot/L"settings.ini",L"InSim",L"Enabled",L"unknown")==L"0"),
            "Another enabled or unverified installation owns activation. Update its existing installation folder, or disable it there before switching");
        return oldRoot;
    }
public:
    explicit LayerRegistration(HKEY scope=HKEY_CURRENT_USER,std::wstring layers=implicitKey,std::wstring owner=integrationOwnerKey):hive(scope),layerKey(std::move(layers)),ownerKey(std::move(owner)){}
    static std::filesystem::path manifest(const std::filesystem::path &root){return std::filesystem::weakly_canonical(root/L"auto/XPFSR24Implicit.json");}
    void repair(const std::filesystem::path &root){
        // Repair is not an opt-in or an installation switch. Recreate only the
        // enabled installation's owned values in the caller's normal user view.
        enable(root,true);
    }
    std::filesystem::path preflight(const std::filesystem::path& root,bool migration=false){
        graphics::require(root.is_absolute(),"Installation path must be absolute");
        NamedLock lock(lockName(ownerKey));return previousOwner(root,migration);
    }
    void enable(const std::filesystem::path &root,bool repairOnly=false,bool migration=false){
        graphics::require(root.is_absolute(),"Installation path must be absolute");
        auto m=manifest(root);graphics::require(std::filesystem::is_regular_file(m)&&std::filesystem::is_regular_file(root/L"layer/XPFSR24Layer.dll"),"The automatic Vulkan layer is missing");
        graphics::require(std::filesystem::is_regular_file(std::filesystem::path(launcher::load(root/L"settings.ini").simulator)/L"X-Plane.exe"),"Select the X-Plane installation first");
        NamedLock lock(lockName(ownerKey));
        if(repairOnly)graphics::require(launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled",L"0")==L"1","Automatic integration is disabled. Use the installation package to enable it first");
        const auto oldRoot=previousOwner(root,migration&&!repairOnly);
        if(!oldRoot.empty())disable(oldRoot); // Same recursive mutex; only verified ownership can move.
        LoaderOrderRegistration(hive,layerKey==implicitKey?L"SOFTWARE\\Khronos\\Vulkan\\LoaderSettings":layerKey+L"\\LoaderSettings",ownerKey).enable(root,std::filesystem::path(launcher::load(root/L"settings.ini").simulator)/L"X-Plane.exe");
        HKEY key{};checked(RegCreateKeyExW(hive,ownerKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr));
        auto rc=RegSetValueExW(key,L"Manifest",0,REG_SZ,reinterpret_cast<const BYTE*>(m.c_str()),DWORD((m.wstring().size()+1)*sizeof(wchar_t)));RegCloseKey(key);checked(rc);
        // Profile flag is the inner gate, so a stale registration never activates
        // a missing/disabled installation. Registration is the final commit.
        graphics::require(launcher::writeSetting(L"InSim",L"Enabled",L"1",(root/L"settings.ini").c_str()),"Cannot save automatic activation");
        checked(RegCreateKeyExW(hive,layerKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr));DWORD enabled=0;
        rc=RegSetValueExW(key,m.c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&enabled),sizeof(enabled));RegCloseKey(key);checked(rc);
    }
    void disable(const std::filesystem::path &root){
        graphics::require(root.is_absolute(),"Installation path must be absolute");
        NamedLock lock(lockName(ownerKey));auto m=manifest(root);
        LoaderOrderRegistration(hive,layerKey==implicitKey?L"SOFTWARE\\Khronos\\Vulkan\\LoaderSettings":layerKey+L"\\LoaderSettings",ownerKey).disable(root);
        HKEY key{};auto rc=RegOpenKeyExW(hive,layerKey.c_str(),0,KEY_SET_VALUE,&key);
        if(rc==ERROR_SUCCESS){rc=RegDeleteValueW(key,m.c_str());RegCloseKey(key);if(rc!=ERROR_FILE_NOT_FOUND)checked(rc);}
        else if(rc!=ERROR_FILE_NOT_FOUND&&rc!=ERROR_PATH_NOT_FOUND)checked(rc);
        auto previous=registryString(hive,ownerKey.c_str(),L"Manifest");
        if(!_wcsicmp(previous.c_str(),m.c_str())){checked(RegOpenKeyExW(hive,ownerKey.c_str(),0,KEY_SET_VALUE,&key));rc=RegDeleteValueW(key,L"Manifest");RegCloseKey(key);if(rc!=ERROR_FILE_NOT_FOUND)checked(rc);}
        if(std::filesystem::exists(root/L"settings.ini"))graphics::require(launcher::writeSetting(L"InSim",L"Enabled",L"0",(root/L"settings.ini").c_str()),"Registration removed but the saved activation flag could not be updated");
    }
};
} // namespace app
