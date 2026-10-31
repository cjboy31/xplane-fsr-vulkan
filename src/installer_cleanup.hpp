// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_integrity.hpp"
#include "launcher_preferences.hpp"
#include "update_feed.hpp"
#include <set>
namespace app {
inline bool ownedGuideMetadata(const std::filesystem::path& file,const std::wstring& version,const std::wstring& digest,uintmax_t bytes){
    if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>8192)return false;
    auto text=graphics::read(file);if(text.rfind("\xef\xbb\xbf",0)==0)text.erase(0,3);
    std::istringstream input(text);std::string line;std::set<std::string> seen;bool section=false;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        if(line=="[Video]"&&!section){section=true;continue;}if(!section)return false;
        const auto equal=line.find('=');if(equal==std::string::npos)return false;auto key=line.substr(0,equal),value=line.substr(equal+1);
        if(!seen.insert(key).second)return false;
        if(key=="Version"){if(value!=std::string(version.begin(),version.end()))return false;}
        else if(key=="SHA256"){if(value!=std::string(digest.begin(),digest.end()))return false;}
        else if(key=="Bytes"){if(value!=std::to_string(bytes))return false;}
        else return false;
    }return section&&seen.count("Version")&&seen.count("SHA256");
}
inline bool retirePreviousGuide(const std::filesystem::path &root,const std::wstring &version,const std::wstring &digest,bool videosRetired=false){
    if(version.empty()||digest.empty())return false;
    updates::version(std::string(version.begin(),version.end()));
    graphics::require(version.size()<80&&digest.size()==64&&std::all_of(digest.begin(),digest.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}),"Invalid previous guide identity");
    const auto media=root/L"media";
    graphics::require(std::filesystem::weakly_canonical(media).parent_path()==std::filesystem::weakly_canonical(root),"Guide directory escaped its installation");
    // A same-version repair must never remove its newly installed movie.
    if(!videosRetired&&launcher::readSetting(media/L"feature-video.ini",L"Video",L"Version")==version)return false;
    const auto old=media/(version+L".mp4");
    if(!std::filesystem::is_regular_file(old))return false;
    graphics::require(std::filesystem::weakly_canonical(old).parent_path()==std::filesystem::weakly_canonical(media),"Previous guide escaped the application media directory");
    if(fileHash(old)!=std::string(digest.begin(),digest.end()))return false;
    const bool metadata=videosRetired&&ownedGuideMetadata(media/L"feature-video.ini",version,digest,std::filesystem::file_size(old));
    const bool removed=std::filesystem::remove(old);
    if(removed&&metadata)std::filesystem::remove(media/L"feature-video.ini");
    if(removed&&std::filesystem::is_empty(media))std::filesystem::remove(media);
    return removed;
}
inline unsigned retireOwnedMedia(const std::filesystem::path& root,const std::filesystem::path& previousManifest){
    if(!std::filesystem::is_regular_file(previousManifest))return 0;
    graphics::require(std::filesystem::file_size(previousManifest)<=2*1024*1024,"Previous payload inventory is oversized");
    auto manifest=updates::Parser(graphics::read(previousManifest)).parse();
    graphics::require(manifest.at("product").str()=="FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)","Previous payload is not this product");
    auto entries=manifest.at("files");graphics::require(entries.type==updates::Json::array,"Invalid previous payload list");unsigned removed=0;
    const auto media=std::filesystem::weakly_canonical(root/L"media");
    graphics::require(media.parent_path()==std::filesystem::weakly_canonical(root),"Media directory escaped its installation");
    for(const auto& entry:entries.items){auto relative=entry.at("path").str();if(relative.rfind("media/",0)!=0)continue;
        auto leaf=relative.substr(6);graphics::require(!leaf.empty()&&leaf.find_first_of("/\\:")==std::string::npos&&leaf!="."&&leaf!="..","Invalid previous media path");
        const auto file=root/L"media"/leaf;if(!std::filesystem::is_regular_file(file))continue;
        graphics::require(std::filesystem::weakly_canonical(file).parent_path()==media,"Previous media escaped its folder");
        if(fileHash(file)==entry.at("sha256").str()&&std::filesystem::remove(file))++removed;
    }
    if(std::filesystem::is_directory(root/L"media")&&std::filesystem::is_empty(root/L"media"))std::filesystem::remove(root/L"media");return removed;
}
}
