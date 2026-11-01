// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_integrity.hpp"
#include "installation_link.hpp"
#include "settings_transaction.hpp"
#include <algorithm>
namespace app {
inline bool publishedPlugin(const std::string &sha){
    constexpr const char *known[]={"bf790e7915cd7afd125daf297eac12d36557f69c0a29d66dbb396522995826a0","d799da598058143fc99f63180da8a66d4cbe078bd6f105fd66a4a38b81a1d888","5fb87828f94aca906601703d49f0626aeac85e38ebf7b068b00867cd18535720","65cc058436aefcc810530b441cf29b6ee3093b2230a7b5b543f5a2b2a3eb1e01","370ee0ff5119aba274755d7f0ed67825b338df35b13c8665db8ba4af229e5b96","6b0b17f2d315b5d50018dd167e896cd54a814ad11aa03ad6d94d2a14ed882e3a","119db0cae84dc5873c31eb42bb3afddd90c4a34c1fa2a949a1c056d394dc1045","d4b42f7931a0481aaa905ec159cbe5e4b8027c7206f5f14f7031cb15b777b94d"};
    return std::find(std::begin(known),std::end(known),sha)!=std::end(known);
}
inline void installPlugin(const std::filesystem::path &app,const std::filesystem::path &sim){
    auto target=sim/L"Resources/plugins/XPFSR24/64/win.xpl",source=app/L"plugin/64/win.xpl";
    const auto current=fileHash(source);std::string old;
    if(std::filesystem::exists(target)){
        old=fileHash(target);
        auto recorded=launcher::readSetting(app/L"settings.ini",L"InSim",L"PluginSHA256");
        graphics::require(old==current||publishedPlugin(old)||old==std::string(recorded.begin(),recorded.end()),"Installed XPFSR24 plugin is not a recognized owned build; no file was replaced");
        if(old!=current){
            auto backup=app/L"plugin-backups"/(L"camera-previous-"+std::wstring(old.begin(),old.end())+L".xpl");std::filesystem::create_directories(backup.parent_path());
            if(!std::filesystem::exists(backup))std::filesystem::copy_file(target,backup);
            graphics::require(fileHash(backup)==old,"Plugin backup differs from the recognized build");
        }
    }
    if(old!=current){
        std::filesystem::create_directories(target.parent_path());
        auto temporary=target;temporary+=L".new-"+std::to_wstring(GetCurrentProcessId());
        graphics::require(!std::filesystem::exists(temporary),"A previous plugin staging file remains");
        std::filesystem::copy_file(source,temporary);
        struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code ec;std::filesystem::remove(file,ec);}}cleanup{temporary};
        graphics::require(fileHash(temporary)==current,"Plugin staging hash mismatch");
        graphics::require(old.empty()?!std::filesystem::exists(target):fileHash(target)==old,"Installed plugin changed during setup");
        graphics::require(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Close X-Plane before updating its plugin");
    }
    std::wstring digest(current.begin(),current.end());
    graphics::require(launcher::writeSetting(L"InSim",L"PluginSHA256",digest.c_str(),(app/L"settings.ini").c_str()),"Could not save installed plugin ownership");
}
inline bool removeOwnedPlugin(const std::filesystem::path &app,const std::filesystem::path &sim){
    // Never delete an edited/unrelated plugin, even from our named directory.
    if(sim.empty())return false;
    auto target=sim/L"Resources/plugins/XPFSR24/64/win.xpl";
    if(!std::filesystem::is_regular_file(target))return false;
    const auto old=fileHash(target);
    const auto recorded=launcher::readSetting(app/L"settings.ini",L"InSim",L"PluginSHA256");
    const auto source=app/L"plugin/64/win.xpl";
    const bool current=std::filesystem::is_regular_file(source)&&old==fileHash(source);
    if(!current&&!publishedPlugin(old)&&old!=std::string(recorded.begin(),recorded.end()))return false;
    graphics::require(std::filesystem::remove(target),"Close X-Plane before removing its plugin");
    removeInstallationLink(app,sim);
    std::error_code ec;std::filesystem::remove(target.parent_path(),ec);
    std::filesystem::remove(target.parent_path().parent_path(),ec);
    return true;
}
} // namespace app
