// SPDX-License-Identifier: MIT
#pragma once
#include "frame_pacing.hpp"
#include "launcher_preferences.hpp"
#include "session_logs.hpp"
#include "framegen_policy.hpp"
#include "nr_policy.hpp"
namespace app {
inline std::wstring environment(const wchar_t *key){
    wchar_t value[32768]{};auto n=GetEnvironmentVariableW(key,value,32768);
    return n&&n<32768?std::wstring(value,n):std::wstring{};
}
inline void setEnvironment(const wchar_t *key,const std::wstring &value){
    if(!SetEnvironmentVariableW(key,value.empty()?nullptr:value.c_str()))throw std::runtime_error("Cannot configure renderer environment");
}
inline bool exactSimulator(const std::filesystem::path &executable,const launcher::Preferences &p){
    std::error_code ec;
    return executable.is_absolute()&&std::filesystem::is_regular_file(executable,ec)&&!ec&&
        std::filesystem::equivalent(executable,std::filesystem::path(p.simulator)/L"X-Plane.exe",ec)&&!ec;
}
inline std::filesystem::path processExecutable(){
    wchar_t value[32768]{};auto n=GetModuleFileNameW(nullptr,value,32768);
    return n&&n<32768?std::filesystem::path(std::wstring(value,n)):std::filesystem::path{};
}
inline void validateSessionProfile(const launcher::Preferences &p){
    const int fg=effectiveFrameGeneration(p.frameGeneration),nr=effectiveNeuralRendering(p.neuralRendering);
    graphics::require(p.mode>=0&&p.mode<=4,"Invalid FSR provider selection");
    graphics::require(p.mode||(!fg&&!nr),"Select FSR Native AA or upscaling before enabling NR/FG");
    graphics::require(!nr||!fg||p.nr[xpfsr::nr::AllowFG],"NR + FG requires explicit opt-in in NR settings");
    graphics::require(!fg||!p.cockpitRestore,"Turn cockpit-screen restore Off for frame generation");
    graphics::require(!p.manualPending,"Apply pending graphics settings with X-Plane closed");
    graphics::require(p.nr.valid(),"Invalid live NR settings");
}
// Used by both launch paths. This does not change simulator resolution/UI,
// launch arguments, global loader variables, registry values or another layer.
inline std::string prepareSessionProfile(const std::filesystem::path &root,const launcher::Preferences &p,
                                        xpfsr::nr::ControlChannel &channel,bool native=false){
    if(!native)validateSessionProfile(p);
    const int mode=native?0:p.mode,fg=native?0:effectiveFrameGeneration(p.frameGeneration),nr=native?0:effectiveNeuralRendering(p.neuralRendering);
    std::ostringstream choices;choices<<"requested_mode="<<mode<<" provider="<<(mode>=3?"4.1.2":"3.1.6")
        <<" frame_generation="<<fg<<" neural_rendering="<<nr<<" native_comparison="<<native
        <<" motion_source="<<(!native&&mode&&p.motionSource==1?"native-renderer":"camera");
    auto token=session::prepare(root/L"logs",choices.str());
    channel.close();
    std::wstring name;
    if(!native&&neuralRenderingAvailable){
        name=L"Local\\XPFSR24-NR-v4-"+std::wstring(token.begin(),token.end());
        auto controls=p.nr;controls[xpfsr::nr::Enabled]=nr!=0;
        graphics::require(channel.create(name.c_str(),controls),"Cannot create live NR control channel");
    }
    setEnvironment(L"XPFSR24_SETTINGS",(root/L"settings.ini").wstring());
    setEnvironment(L"XPFSR24_SESSION",std::wstring(token.begin(),token.end()));
    setEnvironment(L"XPFSR24_NR_CONTROL",name);
    setEnvironment(L"XPFSR24_NR",nr?std::to_wstring(nr):L"");
    setEnvironment(L"XPFSR24_NR_ROOT",(root/L"nr").wstring());
    setEnvironment(L"XPFSR24_NR_STATUS",(root/L"logs/renderer-nr-status.txt").wstring());
    setEnvironment(L"XPFSR24_NR_ALLOW_FG",!native&&p.nr[xpfsr::nr::AllowFG]?L"1":L"");
    setEnvironment(L"XPFSR24_COCKPIT_RESTORE",!native&&p.cockpitRestore?(p.alignmentTint?L"2":L"1"):L"");
    setEnvironment(L"XPFSR24_GHOST_REDUCTION",p.ghostReduction?L"1":L"0");
    const auto *provider=frameGenerationProvider(fg);setEnvironment(L"XPFSR24_FG",provider?provider:L"");
    const bool sync=synchronizedFrameGeneration(fg);
    setEnvironment(L"XPFSR24_FG_PRESENTER",sync?L"3.1.7":L"");
    setEnvironment(L"XPFSR24_FG_SYNCHRONIZED",sync?L"1":L"");
    setEnvironment(L"XPFSR24_FG_PACING",provider?std::to_wstring(std::clamp(p.fgPacing,0,2)):L"");
    setEnvironment(L"XPFSR24_FG_CAP",provider?std::to_wstring(xpfsr::pacing::capSettings[std::clamp(p.fgCap,0,xpfsr::pacing::capCount-1)]):L"");
    // Shared motion contract v1 token; only a reconstruction session uses it.
    setEnvironment(L"XPFSR24_MOTION",!native&&mode&&p.motionSource==1?L"native-renderer":L"");
    setEnvironment(L"XPFSR24_MODE",mode==1||mode==3?L"native-aa":mode==2||mode==4?L"upscale":L"observe");
    setEnvironment(L"XPFSR24_PROVIDER",mode>=3?L"4.1.2":L"3.1.6");
    setEnvironment(L"XPFSR24_RUNTIME",(root/L"runtime").wstring());
    for(auto pair:{std::pair{L"XPFSR24_LOG",L"renderer.log"},{L"XPFSR24_STATUS",L"renderer-status.txt"},
                   {L"XPFSR24_CONTROL",L"renderer-control.txt"},{L"XPFSR24_FG_CONTROL",L"renderer-fg-control.txt"}})
        setEnvironment(pair.first,(root/L"logs"/pair.second).wstring());
    session::write(root/L"logs/renderer-fg-control.txt",fg?"1":"0");
    session::write(root/L"logs/renderer-control.txt",p.enabled?"1":"0");
    // Automatic integration has an exact-X-Plane loader order. Do not change
    // capture discovery halfway through instance construction. Legacy explicit
    // launch still sets its process-local opt-out before discovery begins.
    if(fg&&environment(L"XPFSR24_AUTOMATIC")!=L"1")setEnvironment(L"DISABLE_VULKAN_OBS_CAPTURE",L"1");
    return token;
}
} // namespace app
