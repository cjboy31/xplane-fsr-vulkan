// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
#include "runtime_integrity.hpp"
namespace app {
inline auto installationLinkFile(const std::filesystem::path& simulator){return simulator/L"Resources/plugins/XPFSR24/installation-link.ini";}
inline std::string installationLinkContents(const std::filesystem::path& root,const std::string& plugin){
    const auto encoded=launcher::encode(std::filesystem::weakly_canonical(root).wstring());
    return "XPFSR24InstallationLink=1\nroot="+std::string(encoded.begin(),encoded.end())+"\nplugin="+plugin+"\n";
}
struct InstallationLink {std::filesystem::path root;std::string plugin;};
inline InstallationLink parseInstallationLink(const std::filesystem::path& file){
    std::error_code ec;const auto state=std::filesystem::symlink_status(file,ec);
    if(state.type()==std::filesystem::file_type::not_found)return {};
    graphics::require(!ec&&std::filesystem::is_regular_file(state)&&std::filesystem::file_size(file)<=140000,"The installed setup link is unreadable or redirected");
    const auto attributes=GetFileAttributesW(file.c_str());
    graphics::require(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"The installed setup link is redirected");
    auto text=graphics::read(file);text.erase(std::remove(text.begin(),text.end(),'\r'),text.end());
    std::istringstream in(text);std::string magic,path,digest,extra;
    graphics::require(bool(std::getline(in,magic))&&bool(std::getline(in,path))&&bool(std::getline(in,digest))&&!std::getline(in,extra)&&
        magic=="XPFSR24InstallationLink=1"&&path.rfind("root=",0)==0&&digest.rfind("plugin=",0)==0&&text.back()=='\n',"The installed setup link has an unknown format");
    auto hex=path.substr(5),plugin=digest.substr(7);
    graphics::require(!hex.empty()&&hex.size()%4==0&&hex.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos&&plugin.size()==64&&plugin.find_first_not_of("0123456789abcdef")==std::string::npos,"The installed setup link is invalid");
    auto root=std::filesystem::path(launcher::decode(std::wstring(hex.begin(),hex.end()),{}));
    graphics::require(root.is_absolute(),"The installed setup link must name an absolute installation");
    return {root,plugin};
}
inline std::filesystem::path linkedSettings(const std::filesystem::path& executable){
    if(!executable.is_absolute())return {};
    const auto link=parseInstallationLink(installationLinkFile(executable.parent_path()));if(link.root.empty())return {};
    const auto file=link.root/L"settings.ini";
    graphics::require(std::filesystem::is_regular_file(file)&&exactSimulator(executable,launcher::load(file)),"The installed setup link does not match this simulator");
    graphics::require(launcher::readSetting(file,L"InSim",L"Enabled",L"0")==L"1","The linked installation has in-sim integration disabled");
    const auto owned=launcher::readSetting(file,L"InSim",L"PluginSHA256");
    graphics::require(std::string(owned.begin(),owned.end())==link.plugin&&
        fileHash(executable.parent_path()/L"Resources/plugins/XPFSR24/64/win.xpl")==link.plugin&&
        fileHash(link.root/L"plugin/64/win.xpl")==link.plugin,"The installed setup link does not match the installed plugin; reinstall the complete package");
    return std::filesystem::weakly_canonical(file);
}
inline void writeInstallationLink(const std::filesystem::path& root,const std::filesystem::path& simulator){
    const auto file=root/L"settings.ini";
    graphics::require(exactSimulator(simulator/L"X-Plane.exe",launcher::load(file))&&launcher::readSetting(file,L"InSim",L"Enabled",L"0")==L"1","Enable this installation before saving its setup link");
    const auto plugin=fileHash(root/L"plugin/64/win.xpl");const auto target=installationLinkFile(simulator);
    const auto recorded=launcher::readSetting(file,L"InSim",L"PluginSHA256");
    graphics::require(fileHash(simulator/L"Resources/plugins/XPFSR24/64/win.xpl")==plugin&&std::string(recorded.begin(),recorded.end())==plugin,"Installed plugin ownership does not match the setup link");
    parseInstallationLink(target); // Missing is fine; keep unknown/edited formats.
    graphics::atomicWrite(target,installationLinkContents(root,plugin));
}
inline void removeInstallationLink(const std::filesystem::path& root,const std::filesystem::path& simulator){
    const auto file=installationLinkFile(simulator);if(!std::filesystem::is_regular_file(file)||std::filesystem::is_symlink(file))return;
    const auto recorded=launcher::readSetting(root/L"settings.ini",L"InSim",L"PluginSHA256");
    if(!recorded.empty()&&graphics::read(file)==installationLinkContents(root,std::string(recorded.begin(),recorded.end())))std::filesystem::remove(file);
}
}
