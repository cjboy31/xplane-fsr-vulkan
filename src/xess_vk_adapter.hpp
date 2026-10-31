// SPDX-License-Identifier: MIT
// Original adapter using Intel's external SDK headers and unmodified runtime.
// Not wired into the simulator yet. Context owners must await GPU completion.
#pragma once
#include <xess/xess_vk.h>
#include "runtime_integrity.hpp"
#include <vector>
namespace xpfsr::intel {
inline void need(bool ok,const char*what){if(!ok)throw std::runtime_error(what);}
inline void checked(xess_result_t rc,const char*what){if(rc<0)throw std::runtime_error(std::string(what)+": XeSS result "+std::to_string(rc));}
class Api {
    HMODULE library{};
    template<class T>T symbol(const char*name){auto p=GetProcAddress(library,name);if(!p)throw std::runtime_error(std::string("Missing Intel API: ")+name);return reinterpret_cast<T>(p);}
public:
    decltype(&xessGetVersion) version{};
    decltype(&xessVKGetRequiredInstanceExtensions) instanceRequirements{};
    decltype(&xessVKGetRequiredDeviceExtensions) deviceRequirements{};
    decltype(&xessVKGetRequiredDeviceFeatures) featureRequirements{};
    decltype(&xessVKCreateContext) create{};
    decltype(&xessVKInit) initialize{};
    decltype(&xessGetOptimalInputResolution) inputResolution{};
    decltype(&xessSetVelocityScale) velocityScale{};
    decltype(&xessSetJitterScale) jitterScale{};
    decltype(&xessVKExecute) execute{};
    decltype(&xessDestroyContext) destroy{};
    explicit Api(const std::filesystem::path&file){
        need(file.is_absolute()&&file.filename()==L"libxess.dll","Select the pinned Intel libxess.dll by absolute path");
        need(app::fileHash(file)=="251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7","Unreviewed Intel runtime; expected XeSS SDK v3.0.2 file");
        library=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        need(library!=nullptr,"Intel XeSS runtime could not load");
        try{
#define XP_INTEL_API(member,name) member=symbol<decltype(member)>(name)
            XP_INTEL_API(version,"xessGetVersion");XP_INTEL_API(instanceRequirements,"xessVKGetRequiredInstanceExtensions");
            XP_INTEL_API(deviceRequirements,"xessVKGetRequiredDeviceExtensions");XP_INTEL_API(featureRequirements,"xessVKGetRequiredDeviceFeatures");
            XP_INTEL_API(create,"xessVKCreateContext");XP_INTEL_API(initialize,"xessVKInit");XP_INTEL_API(inputResolution,"xessGetOptimalInputResolution");
            XP_INTEL_API(velocityScale,"xessSetVelocityScale");XP_INTEL_API(jitterScale,"xessSetJitterScale");XP_INTEL_API(execute,"xessVKExecute");XP_INTEL_API(destroy,"xessDestroyContext");
#undef XP_INTEL_API
        }catch(...){FreeLibrary(library);library=nullptr;throw;}
    }
    Api(const Api&)=delete;
    std::vector<const char*> instanceExtensions(uint32_t&minimum)const{uint32_t n{};const char*const*names{};checked(instanceRequirements(&n,&names,&minimum),"XeSS instance requirements");need(n<=128&&(!n||names),"Invalid Intel instance requirement list");return n?std::vector<const char*>(names,names+n):std::vector<const char*>{};}
    std::vector<const char*> deviceExtensions(VkInstance i,VkPhysicalDevice p)const{uint32_t n{};const char*const*names{};checked(deviceRequirements(i,p,&n,&names),"XeSS device requirements");need(n<=128&&(!n||names),"Invalid Intel device requirement list");return n?std::vector<const char*>(names,names+n):std::vector<const char*>{};}
    ~Api(){if(library)FreeLibrary(library);}
};
class Context {
    const Api&api;VkDevice owner{};xess_context_handle_t handle{};xess_2d_t output{};
public:
    Context(const Api&a,VkInstance i,VkPhysicalDevice p,VkDevice d):api(a),owner(d){checked(api.create(i,p,d,&handle),"Create Vulkan XeSS context");}
    Context(const Context&)=delete;
    xess_2d_t initialize(uint32_t w,uint32_t h,xess_quality_settings_t quality){
        need(w&&h&&w<=4096&&h<=4096,"Invalid XeSS output extent");
        output={w,h};xess_2d_t optimal{},minimum{},maximum{};
        checked(api.inputResolution(handle,&output,quality,&optimal,&minimum,&maximum),"Query Intel input resolution");
        xess_vk_init_params_t init{};init.outputResolution=output;init.qualitySetting=quality;
        checked(api.initialize(handle,&init),"Initialize Vulkan XeSS");
        checked(api.velocityScale(handle,1,1),"Set pixel-space Intel motion");checked(api.jitterScale(handle,1,1),"Set render-pixel Intel jitter");return optimal;
    }
    void record(VkCommandBuffer cb,const xess_vk_execute_params_t&p){
        need(cb&&p.colorTexture.image&&p.velocityTexture.image&&p.depthTexture.image&&p.outputTexture.image,"Missing Intel frame resources");
        need(p.outputTexture.width==output.x&&p.outputTexture.height==output.y&&p.inputWidth&&p.inputHeight,"Intel frame dimensions mismatch");
        checked(api.execute(handle,cb,&p),"Record Vulkan XeSS");
    }
    ~Context(){if(handle){vkDeviceWaitIdle(owner);api.destroy(handle);}}
};
}
