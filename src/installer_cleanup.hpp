// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_integrity.hpp"
#include "launcher_preferences.hpp"
#include "update_feed.hpp"
namespace app {
inline bool retirePreviousGuide(const std::filesystem::path &root,const std::wstring &version,const std::wstring &digest,bool videosRetired=false){
    if(version.empty()||digest.empty())return false;
    updates::version(std::string(version.begin(),version.end()));
    graphics::require(version.size()<80&&digest.size()==64&&std::all_of(digest.begin(),digest.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}),"Invalid previous guide identity");
    const auto media=root/L"media";
    // A same-version repair must never remove its newly installed movie.
    if(!videosRetired&&launcher::readSetting(media/L"feature-video.ini",L"Video",L"Version")==version)return false;
    const auto old=media/(version+L".mp4");
    if(!std::filesystem::is_regular_file(old))return false;
    graphics::require(std::filesystem::weakly_canonical(old).parent_path()==std::filesystem::weakly_canonical(media),"Previous guide escaped the application media directory");
    if(fileHash(old)!=std::string(digest.begin(),digest.end()))return false;
    return std::filesystem::remove(old);
}
}
