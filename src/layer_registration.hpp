// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
#include "named_lock.hpp"
#include "loader_order.hpp"
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
public:
    explicit LayerRegistration(HKEY scope=HKEY_CURRENT_USER,std::wstring layers=implicitKey,std::wstring owner=integrationOwnerKey):hive(scope),layerKey(std::move(layers)),ownerKey(std::move(owner)){}
    static std::filesystem::path manifest(const std::filesystem::path &root){return std::filesystem::weakly_canonical(root/L"auto/XPFSR24Implicit.json");}
    void repair(const std::filesystem::path &root){
        // Repair is not an opt-in or an installation switch. Recreate only the
        // enabled installation's owned values in the caller's normal user view.
        enable(root,true);
    }
    void enable(const std::filesystem::path &root,bool repairOnly=false){
        graphics::require(root.is_absolute(),"Installation path must be absolute");
        auto m=manifest(root);graphics::require(std::filesystem::is_regular_file(m)&&std::filesystem::is_regular_file(root/L"layer/XPFSR24Layer.dll"),"The automatic Vulkan layer is missing");
        graphics::require(std::filesystem::is_regular_file(std::filesystem::path(launcher::load(root/L"settings.ini").simulator)/L"X-Plane.exe"),"Select the X-Plane installation first");
        NamedLock lock(lockName(ownerKey));
        if(repairOnly)graphics::require(launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled",L"0")==L"1","Automatic integration is disabled. Use the installation package to enable it first");
        auto previous=registryString(hive,ownerKey.c_str(),L"Manifest");
        graphics::require(previous.empty()||!_wcsicmp(previous.c_str(),m.c_str()),"Another app installation owns automatic activation. Disable it there before switching installations");
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
