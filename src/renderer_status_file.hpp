// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace launcher {
inline void writeRendererStatus(std::ostream&out,const app::Telemetry&s,uint32_t pid,
                                uint64_t tick,const std::string&token){
 out<<s.reason<<'\n'<<s.provider<<'\n'
    <<s.presents<<' '<<s.recordings<<' '<<s.bypasses<<' '<<s.renderWidth<<' '<<s.renderHeight<<' '<<s.outputWidth<<' '<<s.outputHeight<<' '<<s.active<<'\n'
    <<s.fgProvider<<'\n'<<s.fgPrepared<<' '<<s.fgGenerated<<' '<<s.displayedPresents<<' '<<s.fgActive<<'\n'
    <<s.fgReason<<'\n'<<s.cockpitRecordings<<' '<<s.cockpitActive<<' '<<s.cockpitScreens<<'\n'<<s.cockpitReason<<'\n'
    <<"XPFSR24_STATUS_V1 "<<pid<<' '<<tick<<' '<<token<<'\n';
}
inline bool readRendererStatus(const std::filesystem::path&path,uint32_t pid,
                               const std::string&token,uint64_t now,app::Telemetry&out){
 std::error_code error;auto bytes=std::filesystem::file_size(path,error);
 if(error||bytes>16384||token.size()!=32)return false;
 std::ifstream in(path);app::Telemetry s{};std::string line;
 auto text=[&](auto&dest){if(!std::getline(in,line)||line.size()>=sizeof(dest))return false;memcpy(dest,line.c_str(),line.size()+1);return true;};
 if(!text(s.reason)||!text(s.provider)||!(in>>s.presents>>s.recordings>>s.bypasses>>s.renderWidth>>s.renderHeight>>s.outputWidth>>s.outputHeight>>s.active))return false;
 std::getline(in,line);
 if(!text(s.fgProvider)||!(in>>s.fgPrepared>>s.fgGenerated>>s.displayedPresents>>s.fgActive))return false;
 std::getline(in,line);if(!text(s.fgReason)||!(in>>s.cockpitRecordings>>s.cockpitActive>>s.cockpitScreens))return false;
 std::getline(in,line);if(!text(s.cockpitReason))return false;
 std::string marker,session;uint32_t recordedPid{};uint64_t tick{};
 if(!(in>>marker>>recordedPid>>tick>>session)||marker!="XPFSR24_STATUS_V1"||recordedPid!=pid||session!=token||tick>now||now-tick>3000||s.active>1||s.fgActive>1||s.cockpitActive>1)return false;
 out=s;return true;
}
}
