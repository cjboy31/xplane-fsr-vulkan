// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace xpfsr::gpu {
inline std::string normalizedChoice(std::string value){
    const auto begin=value.find_first_not_of(" \t\r\n"),end=value.find_last_not_of(" \t\r\n");
    value=begin==std::string::npos?std::string{}:value.substr(begin,end-begin+1);
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(value=="render")return value;
    if(value.size()!=32||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("GPU choice must be Render or a device UUID");
    return value;
}
struct GpuChoices {std::string neural="render",frameGeneration="render";};
inline std::string encodeChoices(const GpuChoices&c){return "[ProcessingGpu]\nNRUuid="+normalizedChoice(c.neural)+"\nFGUuid="+normalizedChoice(c.frameGeneration)+"\n";}
inline GpuChoices decodeChoices(const std::string&text){
    GpuChoices c;std::istringstream input(text);std::string line;bool section=false;
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#'||line[0]==';')continue;
        if(line[0]=='['){section=line=="[ProcessingGpu]";continue;}if(!section)continue;auto equal=line.find('=');if(equal==std::string::npos)continue;
        auto key=line.substr(0,equal);if(key=="NRUuid")c.neural=normalizedChoice(line.substr(equal+1));else if(key=="FGUuid")c.frameGeneration=normalizedChoice(line.substr(equal+1));
    }return c;
}
enum class GpuTask {Neural,FrameGeneration};
struct Adapter {std::string uuid,name,driverTag;bool transfer{},neural{},frameGeneration{};};
struct LinkProof {std::string render,processing,renderDriver,processingDriver;uint64_t session{};bool distinctPhysical{},transport{},neural{},frameGeneration{};};
enum class SelectionReason {Native,Active,InvalidChoice,RenderMissing,ProcessingMissing,FeatureUnavailable,TransportUnverified,BackendUnverified};
struct Selection {std::string requested,effective;bool backendAllowed{},offloaded{};SelectionReason reason=SelectionReason::Native;};
inline Selection selectGpu(const std::string&desired,GpuTask task,const std::string&renderUuid,const std::vector<Adapter>&adapters,const std::vector<LinkProof>&proofs,uint64_t session){
    Selection result{desired,renderUuid};
    auto render=std::find_if(adapters.begin(),adapters.end(),[&](const auto&a){return a.uuid==renderUuid;});
    if(render==adapters.end()){result.reason=SelectionReason::RenderMissing;return result;}
    auto supported=[&](const Adapter&a){return task==GpuTask::Neural?a.neural:a.frameGeneration;};result.backendAllowed=supported(*render);
    std::string normalized;try{normalized=normalizedChoice(desired);}catch(...){result.reason=SelectionReason::InvalidChoice;return result;}
    if(normalized=="render"||normalized==renderUuid){result.reason=result.backendAllowed?SelectionReason::Native:SelectionReason::FeatureUnavailable;return result;}
    auto processing=std::find_if(adapters.begin(),adapters.end(),[&](const auto&a){return a.uuid==normalized;});
    if(processing==adapters.end()){result.reason=SelectionReason::ProcessingMissing;return result;}
    if(!supported(*processing)||!processing->transfer){result.reason=SelectionReason::FeatureUnavailable;return result;}
    auto proof=std::find_if(proofs.begin(),proofs.end(),[&](const auto&p){return p.render==renderUuid&&p.processing==normalized&&p.renderDriver==render->driverTag&&p.processingDriver==processing->driverTag&&p.session==session&&p.distinctPhysical&&p.transport;});
    if(proof==proofs.end()){result.reason=SelectionReason::TransportUnverified;return result;}
    if(!(task==GpuTask::Neural?proof->neural:proof->frameGeneration)){result.reason=SelectionReason::BackendUnverified;return result;}
    result.effective=normalized;result.offloaded=result.backendAllowed=true;result.reason=SelectionReason::Active;return result;
}
inline const char*selectionMessage(SelectionReason reason){switch(reason){
    case SelectionReason::Native:return "Using X-Plane's render GPU";
    case SelectionReason::Active:return "Verified processing GPU selected";
    case SelectionReason::InvalidChoice:return "Invalid saved GPU choice; using the render GPU";
    case SelectionReason::RenderMissing:return "The simulator's GPU is unavailable; processing cannot start";
    case SelectionReason::ProcessingMissing:return "The selected processing GPU is unavailable; using the render GPU";
    case SelectionReason::FeatureUnavailable:return "The requested feature is unsupported on this GPU; using the render GPU where available";
    case SelectionReason::TransportUnverified:return "Cross-GPU transfer has not been verified for this device/driver session; using the render GPU";
    case SelectionReason::BackendUnverified:return "Transfer works, but this NR/FG offload backend is not verified; using the render GPU";
}return "GPU selection unavailable";}
} // namespace xpfsr::gpu
