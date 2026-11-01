// SPDX-License-Identifier: MIT
#pragma once
#include "session_profile.hpp"
#include "runtime_integrity.hpp"
#include "running_profile.hpp"
#include "named_lock.hpp"
#include "renderer_selection.hpp"
#include "loader_order.hpp"
#include "reconstruction_plugins.hpp"
#include <memory>
namespace app {
// Implicit discovery loads a DLL into many processes. Only the exact selected
// executable and an explicitly enabled saved installation may activate it.
inline bool automaticHost(const std::filesystem::path &root,const std::filesystem::path &exe){
    if(!environment(L"DISABLE_XPFSR24").empty()||!root.is_absolute())return false;
    std::error_code ec;const auto file=root/L"settings.ini";
    if(!std::filesystem::is_regular_file(file,ec)||ec)return false;
    return launcher::readSetting(file,L"InSim",L"Enabled",L"0")==L"1"&&exactSimulator(exe,launcher::load(file));
}
inline bool explicitHost(){
    // Preserve intentional explicit SDK/GPU fixtures as well as the legacy
    // launcher. Registry discovery alone never satisfies this path.
    auto names=environment(L"VK_INSTANCE_LAYERS");bool selected=false;
    std::wistringstream input(names);std::wstring name;while(std::getline(input,name,L';'))selected|=name==L"VK_LAYER_XPFSR24";
    if(!selected||!environment(L"DISABLE_XPFSR24").empty()||!std::filesystem::path(environment(L"XPFSR24_RUNTIME")).is_absolute())return false;
    auto settings=environment(L"XPFSR24_SETTINGS");
    return settings.empty()||exactSimulator(processExecutable(),launcher::load(settings));
}
class Activation {
    xpfsr::nr::ControlChannel controls;
    HANDLE owner{};
    bool matched{};
    bool ownsSession{};
    void configurationOnly(const std::filesystem::path &root) noexcept{
        if(!matched)return;
        try{
            auto token=ownsSession?session::prepare(root/L"logs","activation=bypassed reason="+error):session::id();
            if(ownsSession)session::write(root/L"logs/renderer.log",session::header(token,"activation-bypassed")+"Renderer inactive: "+error+"\n");
            auto wide=std::wstring(token.begin(),token.end());
            SetEnvironmentVariableW(L"XPFSR24_CONFIG_SESSION",wide.c_str());
            if(ownsSession)SetEnvironmentVariableW(L"XPFSR24_SESSION",wide.c_str());
            std::wstring reason(error.begin(),error.end());SetEnvironmentVariableW(L"XPFSR24_ACTIVATION_ERROR",reason.c_str());
            SetEnvironmentVariableW(L"XPFSR24_SETTINGS",(root/L"settings.ini").c_str());
            SetEnvironmentVariableW(L"XPFSR24_NR_CONTROL",nullptr);
        }catch(...){}
    }
public:
    bool active{};std::string error;
    ~Activation(){if(owner)CloseHandle(owner);}
    void initialize(const std::filesystem::path &root,const std::filesystem::path &exe) noexcept{
        try{
            if(explicitHost()){
                const auto decision=xp_renderer::claim(exe.parent_path(),xp_renderer::Choice::fsr,true);
                graphics::require(decision.allowed,decision.reason.c_str());active=true;return;
            }
            if(!automaticHost(root,exe))return;
            matched=true;
            const auto decision=xp_renderer::claim(exe.parent_path(),xp_renderer::Choice::fsr);
            graphics::require(decision.allowed,decision.reason.c_str());
            setEnvironment(L"XPFSR24_AUTOMATIC",L"1");
            setEnvironment(L"XPFSR24_CAPTURE_ORDERED",LoaderOrderRegistration{}.active(root,exe)?L"1":L"");
            // The panel must remain reachable to correct a saved profile even
            // when renderer validation fails for this session.
            setEnvironment(L"XPFSR24_SETTINGS",(root/L"settings.ini").wstring());
            // Keep a second simulator session from resetting the first one's
            // owned logs/IPC. No registration or Vulkan calls occur here.
            const auto name=lockName((root/L"logs/active-session").wstring())+L"-presence";
            owner=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,1,name.c_str());
            const bool existed=GetLastError()==ERROR_ALREADY_EXISTS;
            graphics::require(owner&&!existed,"A renderer session already owns this installation's logs");
            ownsSession=true;
            auto saved=launcher::load(root/L"settings.ini");
            const bool captureLoaded=GetModuleHandleW(L"graphics-hook64.dll")!=nullptr;
            auto current=saved.mode?graphics::current(saved.simulator):graphics::Options{};
            auto p=runningProfile(saved,current,captureLoaded);validateSessionProfile(p);
            setEnvironment(L"XPFSR24_PENDING_GRAPHICS",runningGraphicsNotice(saved,p));
            setEnvironment(L"XPFSR24_FG_GUARD",captureLoaded&&saved.frameGeneration?L"FG capture compatibility selected. The requested frame-generation algorithm is retained; the synchronized presenter will be checked during startup.":L"");
            if(p.mode){
                graphics::require(current.fxaa==0,"Set FXAA Off in X-Plane for FSR");
                graphics::require((p.mode==1||p.mode==3)?current.scale==0:current.scale>0,"X-Plane input scale does not match the running FSR mode");}
            graphics::require(!std::filesystem::exists(std::filesystem::path(p.simulator)/L"vulkan-1.dll"),"Another Vulkan proxy is installed");
            const auto conflict=reconstructionConflict(p.simulator);
            graphics::require(conflict.empty(),conflict.c_str());
            verifyRuntime(root/L"runtime",effectiveFrameGeneration(p.frameGeneration)!=0);
            prepareSessionProfile(root,p,controls);active=true;
        }catch(const std::exception &e){error=e.what();if(owner&&!ownsSession){CloseHandle(owner);owner=nullptr;}configurationOnly(root);OutputDebugStringA(("XPFSR24 automatic activation bypassed: "+error+"\n").c_str());}
        catch(...){error="Automatic activation could not be completed";if(owner&&!ownsSession){CloseHandle(owner);owner=nullptr;}configurationOnly(root);OutputDebugStringA("XPFSR24 automatic activation bypassed\n");}
    }
};
} // namespace app
