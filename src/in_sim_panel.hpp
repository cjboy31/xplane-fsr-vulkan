// SPDX-License-Identifier: MIT
#pragma once
#include "settings_transaction.hpp"
#include "shared.hpp"
#include "changelog.hpp"
#include "product_names.hpp"
#include "framegen_policy.hpp"
#include "renderer_selection.hpp"
#include "frame_pacing.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
namespace insim {
using namespace xpfsr::nr;
inline std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline constexpr auto &fsrNames=product::fsrModes;
inline constexpr auto &fgNames=product::fgModes;
inline constexpr const char *motionSourceNames[]={"Camera","Native (experimental)"};
inline std::string valueName(const Controls &c,Field f){
    int value=c[f];
    if(f==Enabled||f==Mask||f==Grade||f==AllowFG)return value?"On":"Off";
    if(f==Scope)return value?"Whole scene":"Outside only";
    if(f==Order)return value?"After upscaling":"Before upscaling";
    if(f==Style){const char *names[]={"Standard","Natural","Cinematic"};return names[value];}
    if(f==Transfer){const char *names[]={"Matched residual","Edge-aware","Classic"};return names[value];}
    if(f==Scaler){const char *names[]={"Bilinear","Catmull-Rom","Lanczos3","FSR 1 EASU"};return names[value];}
    if(f==Model){const char *names[]={"Auto / Small","Small","Original"};return names[value];}
    if(f==Passes)return std::to_string(value)+(value==1?" pass":" passes");
    if(f==Skin&&value<0)return "Auto";
    return std::to_string(value)+"%";
}
struct Rect {int x{},y{},w{},h{};bool contains(int a,int b)const{return a>=x&&a<x+w&&b>=y&&b<y+h;}};
enum class Kind {Tab,Processing,Mode,FrameGen,NRField,Budget,Help,Close,Preset,Graphics,ApplyGraphics,OpenChoice,ChooseOption,CloseChoice,FGToggle,FGProvider,ImportModel,CloudPreset,Renderer,Changelog,Updates,Logs,RepairStartup,Report,FGPacing,FGCap,MotionSource};
inline constexpr int contentTop=162, footerHeight=82, modelSetupHeight=96, sectionHeight=32, rowHeight=46;
inline Rect footerNotice(int width,int height){return {28,height-78,width-56,51};}
inline Rect footerGuidance(int width,int height){return {28,height-23,width-56,17};}
enum class Widget {Label,Button,Toggle,Slider,Choice,Muted,Reading,Section,Rule,Status};
struct Action {Kind kind;int field{},delta{};};
struct Item {Rect rect;std::string text;bool button{},selected{};Action action{Kind::Help,-1,0};Widget widget{Widget::Label};int value{},minimum{},maximum{100};std::string detail{};};
class Panel {
    std::filesystem::path file;
    ControlChannel *channel{};
    std::function<void(const launcher::Preferences&)> publish;
    std::function<void()> queueGraphics;
    std::function<void()> importModel;
    std::function<void()> openUpdater;
    std::function<void()> openLogs;
    std::function<void()> openReports;
    std::function<void()> repairStartup;
    bool liveNR{},sessionFG{},rendererConnected{true};
    std::vector<const graphics::Preset*> displayPresets;
    uint64_t previousRecords{},lastProgress{};bool sawStatus{};
public:
    launcher::Preferences choices;
    xp_renderer::Choice rendererChoice{xp_renderer::Choice::fsr};
    app::Telemetry telemetry{};
    int tab{},scroll{},helpField{-1},helpScroll{},pinnedHelp{-1};std::string notice,activity="PLUGIN LOADED - renderer not confirmed",nrStatus="NR status not available";
    bool dropdownOpen{};Action dropdown{Kind::Mode};int dropdownScroll{};
    bool modelImportBusy{};std::string modelImportStatus="Import your own nvngx_dlssnr.dll 310.9.16 or 310.8.0. Models are not included.";
    std::string reportStatus="Reporting is Off until you agree and choose Send now or Send after this session.";
    Panel()=default;
    void bind(const std::filesystem::path &settings,ControlChannel *controls,bool canNR,bool hasFG,const std::function<void(const launcher::Preferences&)> &onPublish={}){
        file=settings;channel=controls;liveNR=canNR;sessionFG=hasFG;publish=onPublish;refresh();
        displayPresets=graphics::availableResolutionPresets([](int w,int h){return graphics::supportsOutput(w,h);});
    }
    bool configured()const{return !file.empty();}
    bool rendererLinked()const{return configured()&&rendererConnected;}
    std::string frameGenerationStatus()const{
        if(!rendererLinked())return "Not running in this session";
        if(!choices.frameGeneration)return sessionFG?"Off - live switch available":"Off";
        if(!sessionFG)return sawStatus&&telemetry.fgReason[0]?std::string(telemetry.fgReason,strnlen(telemetry.fgReason,sizeof(telemetry.fgReason))):"Restart needed to enable frame generation";
        if(!sawStatus||!lastProgress||activity.find("WAITING")==0)return "Waiting for current-session renderer status";
        const std::string reason(telemetry.fgReason,strnlen(telemetry.fgReason,sizeof(telemetry.fgReason)));
        // The panel itself pauses interpolation. Never call a saved On choice
        // or old generated-frame count proof of generation right now.
        if(reason.find("setup is visible")!=std::string::npos)return "Paused while this panel is open - close it to run";
        if(!telemetry.fgActive)return reason.empty()?"Waiting for a usable scene - see Diagnostics":reason;
        if(!telemetry.fgGenerated||!telemetry.displayedPresents)return "Starting - waiting for generated and presented frames";
        return "Running - "+std::to_string(telemetry.fgGenerated)+" frames generated this session";
    }
    void configurationOnly(const std::string &reason,bool activationBypassed=false){rendererConnected=false;liveNR=false;sessionFG=false;activity=activationBypassed?"RENDERER BYPASSED - see startup reason below":"RENDERER NOT LOADED - next-session settings available";nrStatus="Not running in this session";notice=reason;}
    void graphicsSupport(std::function<void()> callback){queueGraphics=std::move(callback);}
    void updateSupport(std::function<void()> callback){openUpdater=std::move(callback);}
    void logSupport(std::function<void()> callback){openLogs=std::move(callback);}
    void reportSupport(std::function<void()> callback){openReports=std::move(callback);}
    void repairSupport(std::function<void()> callback){repairStartup=std::move(callback);}
    bool needsStartupRepair()const{return configured()&&!rendererConnected&&bool(repairStartup);}
    void modelSupport(std::function<void()> callback){importModel=std::move(callback);}
    void refresh(){if(configured()){choices=launcher::load(file);if(!choices.simulator.empty())try{rendererChoice=xp_renderer::visibleChoice(choices.simulator);}catch(const std::exception& e){rendererChoice=xp_renderer::Choice::off;notice=std::string(e.what())+". Choose Renderer again to repair it, then restart.";}}}
    std::vector<std::string> choiceOptions(Action a)const{
        if(a.kind==Kind::Renderer)return {"Native AA - independent anti-aliasing","FSR / NR / frame generation","Original simulator - both mods Off"};
        if(a.kind==Kind::NRField&&(a.field<0||a.field>=Count))return {};
        if(a.kind==Kind::Mode)return {std::begin(fsrNames),std::end(fsrNames)};
        if(a.kind==Kind::FrameGen)return {std::begin(fgNames),std::end(fgNames)};
        if(a.kind==Kind::FGProvider)return {std::begin(fgNames)+1,std::end(fgNames)};
        if(a.kind==Kind::FGPacing)return {std::begin(xpfsr::pacing::profileNames),std::end(xpfsr::pacing::profileNames)};
        if(a.kind==Kind::FGCap)return {std::begin(xpfsr::pacing::capNames),std::end(xpfsr::pacing::capNames)};
        if(a.kind==Kind::MotionSource)return {std::begin(motionSourceNames),std::end(motionSourceNames)};
        if(a.kind==Kind::CloudPreset)return {"Keep current choices","Cloud clarity - Ultra Quality source","Cloud clarity - Native AA source (highest cost)"};
        if(a.kind==Kind::Preset){std::vector<std::string> names{"Custom"};for(auto p:displayPresets)names.push_back(utf8(p->label));return names;}
        if(a.kind==Kind::NRField){std::vector<std::string> result;auto c=choices.nr;for(int i=settings[a.field].minimum;i<=settings[a.field].maximum;++i){c[Field(a.field)]=i;result.push_back(valueName(c,Field(a.field)));}return result;}
        if(a.kind==Kind::Graphics){if(a.field==0)return {"Off","2x MSAA (experimental)","4x MSAA (experimental)","8x MSAA (experimental)"};if(a.field==8)return {"Keep current cloud quality","Low","Medium","High","Maximum"};if(a.field==2)return {"Full - Native AA","Ultra quality","Quality - 1.5x","Balanced","Performance - 2x"};if(a.field==3){std::vector<std::string> result;for(auto o:graphics::outputChoices)result.push_back(utf8(o.label));return result;}}
        return {};
    }
    int choiceCurrent(Action a)const{
        if(a.kind==Kind::Renderer)return int(rendererChoice)-1;
        if(a.kind==Kind::Mode)return choices.mode;if(a.kind==Kind::FrameGen)return choices.frameGeneration;
        if(a.kind==Kind::FGProvider)return choices.lastFrameGeneration-1;
        if(a.kind==Kind::FGPacing)return std::clamp(choices.fgPacing,0,2);
        if(a.kind==Kind::FGCap)return std::clamp(choices.fgCap,0,xpfsr::pacing::capCount-1);
        if(a.kind==Kind::MotionSource)return std::clamp(choices.motionSource,0,1);
        if(a.kind==Kind::CloudPreset)return 0;
        if(a.kind==Kind::NRField)return choices.nr[Field(a.field)]-settings[a.field].minimum;
        if(a.kind==Kind::Preset){for(size_t i=0;i<displayPresets.size();++i)if(choices.presetId==displayPresets[i]->id)return int(i)+1;return 0;}
        if(a.kind==Kind::Graphics){auto o=choices.presetId.empty()?choices.manual:graphics::presetById(choices.presetId).settings;if(a.field==0)return o.msaa;if(a.field==8)return o.cloudQuality+1;if(a.field==2)return o.scale;if(a.field==3){for(size_t i=0;i<graphics::outputChoices.size();++i)if(o.outputWidth==graphics::outputChoices[i].width&&o.outputHeight==graphics::outputChoices[i].height)return int(i);}}
        return 0;
    }
    void slider(int field,int value){
        if(field<0||field>=Count)throw std::runtime_error("Invalid slider field");
        if(!((field>=Intensity&&field<=History)||field==Scale||field==Passes))throw std::runtime_error("This setting is not a numeric slider");
        refresh();auto p=choices;value=std::clamp(value,settings[field].minimum,settings[field].maximum);
        if(field==Scale){const int scales[]={25,33,50,67,100};value=*std::min_element(std::begin(scales),std::end(scales),[&](int a,int b){return abs(a-value)<abs(b-value);});}
        p.nr[Field(field)]=value;if(p.nr!=choices.nr)persist(p);
    }
    int selectedBudget()const{
        for(int i=0;i<3;++i){auto c=choices.nr;performanceProfile(c,i);if(c[Scale]==choices.nr[Scale]&&c[Passes]==choices.nr[Passes]&&c[Model]==choices.nr[Model]&&c[Transfer]==choices.nr[Transfer])return i;}
        return -1;
    }
    std::string budgetSelectionMessage(int profile)const{
        const char *labels[]={"Performance 33%","Balanced 50%","Quality 67%"};
        std::string message=std::string("Selected: ")+labels[std::clamp(profile,0,2)]+", one pass. Saved. ";
        if(nrStatus.find("model missing")!=std::string::npos||nrStatus.find("integrity mismatch")!=std::string::npos)return message+"Waiting for a model: open Neural rendering > Import NR model DLL, then turn NR Off/On.";
        if(!choices.nr[Enabled])return message+"NR is Off; enable it to use this budget.";
        if(!liveNR)return message+"Queued for a compatible NR session. Restart after configuring FSR.";
        if(nrStatus.find("bypassed")!=std::string::npos||nrStatus.find("unavailable")!=std::string::npos)return message+"NR is bypassed; see its status above. FSR continues.";
        return message+"NR prepares changed model budgets in the background. Its current status is shown above; FSR continues.";
    }
    void observe(const app::Telemetry &s,uint64_t now){
        if(!configured()){activity="SETUP NOT LINKED - renderer not confirmed";return;}
        if(!rendererConnected)return; // Retain the specific missing/bypassed startup state.
        if(sawStatus&&s.recordings>previousRecords)lastProgress=now;previousRecords=s.recordings;sawStatus=true;telemetry=s;
        if(!choices.enabled)activity="PROCESSING PAUSED";
        else if(lastProgress&&now>=lastProgress&&now-lastProgress<3000)activity=s.active?"FSR ACTIVE - renderer confirmed":"RENDERER CONFIRMED - processing protected / waiting";
        else activity="WAITING - load a flight to check image processing";
    }
    void persist(launcher::Preferences proposed){
        if(!configured())throw std::runtime_error("In-sim setup is not linked. Run setup and select this X-Plane installation.");
        if(proposed.nr[Enabled]&&!liveNR&&!proposed.mode)throw std::runtime_error("Select an FSR AA/upscaling profile for the next session before enabling NR.");
        if(proposed.nr[Enabled]&&sessionFG&&!proposed.nr[AllowFG])throw std::runtime_error("Enable Allow NR + frame generation first, or use an FG-Off session.");
        choices=launcher::transactChoices(file,choices,proposed,[&](const auto &merged){if(channel)channel->write(merged.nr);if(publish)publish(merged);});
        notice=liveNR?"Saved. NR/processing controls are live; FSR and FG providers apply next session.":"Saved for the next session. This session has no live NR renderer; correct/queue graphics and restart using your usual launcher.";
    }
    void act(Action action,bool refreshFirst=true){
        if(action.kind==Kind::RepairStartup){if(!needsStartupRepair())throw std::runtime_error("Startup repair is unavailable for this session.");repairStartup();notice="Repairing the startup link. Your flight keeps running; your image settings are kept.";return;}
        if(action.kind==Kind::Logs){if(!openLogs)throw std::runtime_error("Log support is unavailable. Reinstall the complete package.");openLogs();notice="Opened Output / FSR-Logs in your X-Plane folder. A fresh copy is also saved when X-Plane exits.";return;}
        if(action.kind==Kind::Report){if(!openReports)throw std::runtime_error("Report support is unavailable. Use the log folder and Discord for now.");openReports();notice="Report preview opened. Sharing is Off until you agree and choose Send now or Send after this session.";return;}
        if(action.kind==Kind::Changelog){helpField=300;helpScroll=0;dropdownOpen=false;return;}
        if(action.kind==Kind::Updates){if(!openUpdater)throw std::runtime_error("The updater is missing. Reinstall the complete package.");openUpdater();notice="Updater opened. You can check now; close X-Plane before installing.";return;}
        if(action.kind==Kind::Tab){tab=std::clamp(action.field,0,3);scroll=0;helpField=-1;pinnedHelp=-1;dropdownOpen=false;return;}
        // "?" no longer opens a page: hovering shows a card, a click pins/unpins it.
        if(action.kind==Kind::Help){pinnedHelp=pinnedHelp==action.field?-1:action.field;return;}
        if(action.kind==Kind::OpenChoice){dropdown={Kind(action.field),action.delta};dropdownOpen=true;dropdownScroll=0;helpField=-1;return;}
        if(action.kind==Kind::CloseChoice){dropdownOpen=false;return;}
        if(action.kind==Kind::ChooseOption){refresh();auto options=choiceOptions(dropdown);if(action.field<0||action.field>=int(options.size()))throw std::runtime_error("Invalid dropdown selection");auto selected=dropdown;selected.delta=action.field-choiceCurrent(dropdown);if(selected.kind==Kind::NRField)selected.delta=action.field+settings[selected.field].minimum-choices.nr[Field(selected.field)];dropdownOpen=false;act(selected,false);return;}
        if(action.kind==Kind::ApplyGraphics){if(!queueGraphics)throw std::runtime_error("Graphics support is unavailable; reinstall the complete in-sim package.");queueGraphics();notice="Queued. Graphics will apply after you close X-Plane normally. Your current flight continues.";return;}
        if(action.kind==Kind::ImportModel){
            if(modelImportBusy){notice="A model import is already open. Complete or cancel its file selector, then wait for the result.";return;}
            if(!importModel)throw std::runtime_error("Model import support is unavailable; reinstall the complete in-sim package.");
            importModel();return;
        }
        if(action.kind==Kind::Renderer){
            refresh();const int next=int(rendererChoice)+action.delta;
            if(next<1||next>3)throw std::runtime_error("Invalid renderer selection");
            xp_renderer::save(choices.simulator,xp_renderer::Choice(next));rendererChoice=xp_renderer::Choice(next);
            notice=std::string("Saved: ")+xp_renderer::label(rendererChoice)+". Close and restart X-Plane using your usual launcher. Current renderer and both mods' settings are retained.";return;
        }
        if(refreshFirst)refresh();auto p=choices;
        if(action.kind==Kind::Processing)p.enabled=action.delta?action.delta>0:!p.enabled;
        if(action.kind==Kind::Mode){p.mode=(p.mode+action.delta+5)%5;p.presetId.clear();if(!p.mode){p.frameGeneration=0;p.neuralRendering=0;p.nr[Enabled]=0;}}
        if(action.kind==Kind::FGProvider){p.lastFrameGeneration=std::clamp(p.lastFrameGeneration+action.delta,1,4);if(p.frameGeneration)p.frameGeneration=p.lastFrameGeneration;}
        if(action.kind==Kind::FrameGen||action.kind==Kind::FGToggle||action.kind==Kind::FGProvider){
            if(action.kind==Kind::FrameGen)p.frameGeneration=(p.frameGeneration+action.delta+5)%5;
            if(action.kind==Kind::FGToggle)p.frameGeneration=action.delta>0?p.lastFrameGeneration:0;
            if(p.frameGeneration&&!p.mode)throw std::runtime_error("Choose FSR AA/upscaling for the next session first.");if(p.frameGeneration&&p.nr[Enabled]&&!p.nr[AllowFG])throw std::runtime_error("Enable Allow NR + frame generation before queuing this combination.");if(p.frameGeneration){p.cockpitRestore=false;p.lastFrameGeneration=p.frameGeneration;}
        }
        if(action.kind==Kind::FGPacing)p.fgPacing=std::clamp(p.fgPacing+action.delta,0,2);
        if(action.kind==Kind::FGCap)p.fgCap=std::clamp(p.fgCap+action.delta,0,xpfsr::pacing::capCount-1);
        if(action.kind==Kind::MotionSource)p.motionSource=std::clamp(p.motionSource+action.delta,0,1);
        if(action.kind==Kind::Budget)performanceProfile(p.nr,std::clamp(action.field,0,2));
        if(action.kind==Kind::CloudPreset){
            if(action.delta==0){notice="Cloud clarity choices unchanged.";return;}
            if(action.delta<1||action.delta>2)throw std::runtime_error("Invalid cloud clarity setup");
            if(!p.mode)throw std::runtime_error("Select an FSR 3 or FSR 4 profile before choosing a cloud clarity setup");
            auto options=p.presetId.empty()?p.manual:graphics::presetById(p.presetId).settings;
            options.cloudQuality=3;options.fxaa=0;options.scale=action.delta==1?1:0;
            p.mode=(p.mode>=3?3:1)+(options.scale?1:0);launcher::customGraphics(p,options,false);
        }
        if(action.kind==Kind::Preset){
            const auto &available=displayPresets;
            if(available.empty())throw std::runtime_error("No supported display preset was found. Use Custom graphics.");
            int at=-1;for(size_t i=0;i<available.size();++i)if(p.presetId==available[i]->id)at=int(i);
            at=std::clamp(at+action.delta,-1,int(available.size())-1);
            if(at<0)p.presetId.clear();else{const auto &preset=*available[at];p.presetId=preset.id;p.mode=preset.launcherMode;p.manual=preset.settings;p.manual.keepUISize=p.keepSimUISize;p.manualApplied=false;p.manualPending=true;p.frameGeneration=0;}
        }
        if(action.kind==Kind::Graphics){
            auto options=p.presetId.empty()?p.manual:graphics::presetById(p.presetId).settings;
            if(action.field==0)options.msaa=std::clamp(options.msaa+action.delta,0,3);
            else if(action.field==1){options.fxaa=std::clamp(options.fxaa+action.delta,0,1);if(options.fxaa&&p.mode)throw std::runtime_error("FXAA requires FSR processing mode Off. FSR provides temporal AA; MSAA remains an experimental option.");}
            else if(action.field==2)options.scale=std::clamp(options.scale+action.delta,0,4);
            else if(action.field==3){int at=0;for(size_t i=0;i<graphics::outputChoices.size();++i)if(options.outputWidth==graphics::outputChoices[i].width&&options.outputHeight==graphics::outputChoices[i].height)at=int(i);at=std::clamp(at+action.delta,0,int(graphics::outputChoices.size())-1);auto o=graphics::outputChoices[at];if(o.width&&!graphics::supportsOutput(o.width,o.height))throw std::runtime_error("The primary display does not advertise this output resolution.");options.outputWidth=o.width;options.outputHeight=o.height;}
            else if(action.field==4)p.keepSimUISize=action.delta>0;
            else if(action.field==5)p.ghostReduction=action.delta>0;
            else if(action.field==6){p.cockpitRestore=action.delta>0;if(p.cockpitRestore)p.frameGeneration=0;}
            else if(action.field==7){p.alignmentTint=action.delta>0;if(p.alignmentTint){p.cockpitRestore=true;p.frameGeneration=0;}}
            else if(action.field==8)options.cloudQuality=std::clamp(options.cloudQuality+action.delta,-1,3);
            else throw std::runtime_error("Invalid graphics field");
            if(action.field<4||action.field==8){const auto &old=p.manual;bool changed=!p.presetId.empty()||options.msaa!=old.msaa||options.fxaa!=old.fxaa||options.scale!=old.scale||options.cloudQuality!=old.cloudQuality||options.outputWidth!=old.outputWidth||options.outputHeight!=old.outputHeight;if(changed)launcher::customGraphics(p,options,false);}
        }
        if(action.kind==Kind::NRField){
            if(action.field<0||action.field>=Count)throw std::runtime_error("Invalid NR field");auto f=Field(action.field);auto &s=settings[f];
            if(f==Scale){const int scales[]={25,33,50,67,100};int at=0;for(int i=0;i<5;++i)if(p.nr[f]==scales[i])at=i;p.nr[f]=scales[std::clamp(at+action.delta,0,4)];}
            else{int step=(f<=History&&f!=Enabled)?5:1;p.nr[f]=std::clamp(p.nr[f]+action.delta*step,s.minimum,s.maximum);}
            if(f==Enabled)p.neuralRendering=p.nr[Enabled]?4:0;
        }
        if(launcher::serialized(p)!=launcher::serialized(choices))persist(p);
        if(action.kind==Kind::FGToggle)notice=sessionFG?(choices.frameGeneration?"Frame generation On. Close this panel to resume; check Diagnostics for generated frames.":"Frame generation Off. The change is live; your method stays saved."):(choices.frameGeneration?"Frame generation saved On. Restart X-Plane to load its presenter; it is not running yet.":"Frame generation saved Off. Your method stays saved.");
        if(action.kind==Kind::FGProvider)notice="Frame generation method saved. The new method applies after restarting X-Plane. The current method is retained for this flight.";
        if(action.kind==Kind::FGCap)notice=std::string("Frame rate cap saved: ")+xpfsr::pacing::capNames[std::clamp(choices.fgCap,0,xpfsr::pacing::capCount-1)]+". It limits real frames while frame generation runs, after you restart X-Plane.";
        if(action.kind==Kind::MotionSource)notice=choices.motionSource?"Motion source saved: Native (experimental). After you restart X-Plane, FSR uses X-Plane's own motion data when it can. It also works with MSAA, frame generation and the cockpit screen fix. If it cannot run, the normal motion is used and Diagnostics says why.":"Motion source saved: Camera. This is the normal motion. It applies after you restart X-Plane.";
        if(action.kind==Kind::FGPacing)notice=std::string("Frame pacing saved: ")+xpfsr::pacing::profileNames[std::clamp(choices.fgPacing,0,2)]+". It applies the next time X-Plane starts with frame generation.";
        if(action.kind==Kind::Budget)notice=budgetSelectionMessage(action.field);
        if(action.kind==Kind::CloudPreset)notice="Cloud clarity setup saved: Maximum clouds with "+std::string(action.delta==1?"Ultra Quality input.":"full input and Native AA.")+" Select Apply graphics after exit, then restart. Higher source resolution costs GPU time; distant cloud detail remains limited by X-Plane's cloud pass.";
        if(action.kind==Kind::Graphics&&action.field==8)notice="Cloud quality saved for next session. Select Apply graphics after exit. This controls X-Plane's cloud quality; FSR/FG/NR choices are retained.";
    }
    int maxScroll(int height)const{
        if(!configured())return 0;
        const int lengths[]={3*sectionHeight+12*rowHeight+68,2*sectionHeight+modelSetupHeight+Count*rowHeight,
                             144+2*sectionHeight+62+61+48+75+75+48,3*sectionHeight+10*rowHeight+44};
        return std::max(0,lengths[tab]+(tab==0&&needsStartupRepair()?44:0)+8-std::max(1,height-contentTop-footerHeight));
    }
    int maxDropdownScroll(int height)const{return std::max(0,int(choiceOptions(dropdown).size())*36-std::max(1,height-contentTop-footerHeight-68));}
    bool wheel(int amount,int height){auto &position=dropdownOpen?dropdownScroll:scroll;int next=std::clamp(position+amount,0,dropdownOpen?maxDropdownScroll(height):maxScroll(height));if(next==position)return false;position=next;return true;}
    std::string help()const{return helpText(helpField);}
    // Short caption for a hover card. Setting rows use their own label.
    std::string helpTitle(int f)const{
        if(f>=0&&f<Count)return utf8(settings[f].label);
        switch(f){case 100:return "MSAA";case 101:return "FXAA";case 102:return "Input detail";case 103:return "Output resolution";case 104:return "Menu size";case 105:return "FSR 3 trail reduction";
            case 106:return "Cockpit screen protection";case 107:return "Protection tint";case 108:return "Cloud quality";case 200:return "FSR mode";case 201:return "Frame generation";case 202:return "Resolution preset";
            case 203:return "Import NR model";case 204:return "Cloud presets";case 205:return "Renderer";case 206:return "FSR processing";case 207:return "NR quick setting";case 208:return "Repair startup";case 209:return "Frame pacing";case 210:return "Frame rate cap";case 211:return "Motion source";default:return "Setting guide";}
    }
    std::string helpText(int f)const{
        if(f==300)return changelogText();
        if(!configured())return "Rendering controls are unavailable until this simulator is linked to a saved installation. No selections have been changed. Enable in-sim integration for this X-Plane folder, then restart using your usual launcher.";
        if(f>=0&&f<Count)return utf8(settings[f].help);
        if(f==100)return "MSAA smooths the edges of 3D objects. Higher levels use more graphics time and memory. Combining it with FSR is experimental. Start Off or 2x. Apply after exit, then restart.";
        if(f==101)return "FXAA is a fast smoother that can soften text and textures. Keep it Off with FSR Native AA or upscaling. Use it only when the FSR mode is Off.";
        if(f==102)return "Input detail controls how much detail X-Plane draws before FSR. Native AA always starts at Full. Select FSR Upscaling to use lower input settings. At 4K output, Quality starts with a 1440p picture. Lower levels can improve speed but lose detail. Apply after exit, then restart.";
        if(f==103)return "Output resolution is the picture sent to your display. Keep current keeps your existing display setup. Select a supported size, then Apply after exit and restart. A higher setting cannot add pixels your monitor does not have.";
        if(f==104)return "Keep menus and text about the same size when this plugin changes output resolution. Leave this On unless you want X-Plane to choose its own menu size.";
        if(f==105)return "Try to reduce trails left by FSR 3. It can make some thin lines flicker more. This only affects FSR 3 and needs a restart.";
        if(f==106)return "Cockpit screen protection restores supported cockpit displays after FSR. It only works with supported screen layouts, not every aircraft. Frame generation must be Off. Turn this off if the restored area is misplaced or looks wrong.";
        if(f==107)return "Show a green tint on screen areas accepted by cockpit screen protection. Use it to check alignment, then turn it Off. Unsupported layouts may show no tint. This requires frame generation Off and a restart.";
        if(f==108)return "Choose X-Plane's cloud quality. Higher quality can reduce blocky clouds but uses more graphics time. It cannot add every missing distant detail. Apply after exit, then restart. Your weather stays the same.";
        if(f==200)return "Native AA keeps the full input resolution and smooths edges. Upscaling builds a larger picture from a smaller input and can improve speed. Match Input detail on the Image quality tab and keep FXAA Off. Changes need a restart.";
        if(f==201)return "Frame generation adds pictures between real frames to make movement look smoother. It does not speed up the simulation. Start with a steady base frame rate. If this session started with frame generation, On/Off works live. Otherwise restart once to load it. Changing the method needs a restart. The synchronized methods are a compatibility fallback. They wait for every picture to be shown before the next real frame starts, which costs real frame rate. Use them only if the normal method shows flicker, black frames or crashes. It pauses while this panel is open. The status above and Diagnostics show whether frames are really being generated.";
        if(f==202)return "A preset chooses resolution and FSR settings together. It turns MSAA, FXAA and frame generation Off as a starting point. Choose Apply after exit, close X-Plane normally, then reopen it. Custom keeps your separate FSR and frame-generation choices.";
        if(f==203)return "Select your own supported nvngx_dlssnr.dll, version 310.9.16 or 310.8.0. The importer reads the model without running the DLL. Models are not included in this download. Wait for the import result, then turn NR Off and On if needed.";
        if(f==204)return "These cloud presets raise input detail and choose Maximum cloud quality. Higher detail can reduce blockiness but costs graphics time. Native AA is the most demanding choice. Your weather and other effects stay selected. Apply after exit and restart.";
        if(f==205)return "Choose Native AA, the separate FSR mod, or the original simulator for your next launch. Only one mod processes the scene. Close and reopen X-Plane after changing this. Both mods keep your choices. The input scale is prepared after a normal exit; Native AA uses Full.";
        if(f==208)return "Repair startup fixes the Windows link used to load this mod. Your image settings stay saved. Close X-Plane normally and reopen it after repair. It does not turn on an installation you disabled or switch away from another enabled installation. Use the installation package if files are missing.";
        if(f==209)return "Frame pacing decides when each real and generated picture is shown. Smooth uses AMD's default timing. Recover after heavy scenes gets your frame rate back sooner after busy airports or thick clouds, with slightly less even timing. Power saver lets the CPU rest instead of waiting in a busy loop. For the smoothest result, keep your real frame rate a little below half your monitor's refresh rate, use fullscreen or borderless, and turn VSync off with FreeSync on. Applies after restart.";
        if(f==211)return "Motion source decides how FSR learns which way each pixel moved. Camera is the normal choice and works with everything. Native (experimental) reads motion that X-Plane itself works out for its reflections. It knows which pixels belong to your own aircraft, so the cockpit and airframe can trail less when you turn or zoom. It works with MSAA, frame generation and the cockpit screen fix, and feeds all of them. If it cannot run on a frame, the normal motion is used and Diagnostics says why. It does not yet cover other traffic, propellers or clouds. Applies after restart.";
        if(f==210)return "Frame rate cap limits how many real frames X-Plane draws while frame generation runs. Frame generation is smoothest when real frames come at an even rate a little below half your monitor's refresh rate. Auto picks that for you (for example 71 FPS on a 144 Hz screen). Off lets X-Plane run free. Applies after restart.";
        if(f==206)return "Turn FSR image processing on or off during your flight. Off shows the current simulator picture without FSR processing. The FSR and frame-generation methods still need a restart when changed.";
        if(f==207)return "Choose a quick NR speed and detail setting. Performance uses a smaller model picture. Quality uses a larger one. Each uses one pass. Your lighting and color choices stay saved. Wait for the NR status to confirm the change.";
        return tab==3?"Choose your graphics settings, then select Apply after exit. Close X-Plane normally; the background component applies only these settings with a backup. Restart using your usual launcher. FSR/FG choices are retained for custom edits. FXAA is native-only; MSAA + FSR is experimental.":"Live NR and processing controls save automatically. Presets/graphics and FSR/FG providers need a new session. Queue changed graphics on the Image quality tab before exiting. FG pauses while this panel is visible. Open Changelog for changes, or Updates for the separate updater.";
    }
    std::vector<Item> view(int width,int height){
        std::vector<Item> out;
        auto item=[&](Rect r,const std::string &s,Widget type=Widget::Label){
            out.push_back({r,s,false,false,{Kind::Help,-1},type});
        };
        auto button=[&](Rect r,const std::string&s,Action action,bool selected=false,Widget type=Widget::Button){
            out.push_back({r,s,true,selected,action,type});
        };
        button({width-252,18,88,30},"Changelog",{Kind::Changelog});
        if(configured())button({width-158,18,70,30},"Updates",{Kind::Updates});
        button({width-82,18,58,30},"Close",{Kind::Close});
        item({40,60,width-64,20},activity,Widget::Status);
        const int half=(width-56)/2;
        item({24,84,half,20},"NR   /   "+nrStatus,Widget::Muted);
        item({32+half,84,half,20},"FG   /   "+frameGenerationStatus(),Widget::Muted);
        const char *tabs[]={"Rendering","Neural rendering","Diagnostics","Image quality"};
        const int tabWidth=(width-48)/4;
        for(int i=0;i<4;++i)button({24+i*tabWidth,110,tabWidth,36},tabs[i],{Kind::Tab,i},tab==i);

        const int top=contentTop,bottom=height-footerHeight;
        scroll=std::clamp(scroll,0,maxScroll(height));
        int cursor=top-scroll;
        auto visible=[&](int y,int h){return y>=top&&y+h<=bottom;};
        auto section=[&](const char* title,const char* timing=""){
            if(visible(cursor,sectionHeight)){
                item({24,cursor,width-48,sectionHeight},title,Widget::Section);
                out.back().detail=timing;
            }
            cursor+=sectionHeight;
        };
        const int controlX=std::max(232,width/2);
        const int helpX=width-56;
        const int controlWidth=helpX-12-controlX;
        auto row=[&](const std::string& label,const std::string& value,Kind kind,int field,const char* timing){
            const int y=cursor;
            cursor+=rowHeight;
            if(!visible(y,rowHeight))return;
            const bool hasTiming=timing[0]!='\0';
            item({24,y+rowHeight-1,width-48,1},"",Widget::Rule);
            item({28,y+(hasTiming?3:13),controlX-40,20},label);
            if(hasTiming)item({28,y+22,controlX-40,18},timing,Widget::Muted);
            bool toggle=kind==Kind::Processing||kind==Kind::FGToggle||
                (kind==Kind::NRField&&(field==Enabled||field==Mask||field==Grade||field==AllowFG))||
                (kind==Kind::Graphics&&(field==1||(field>=4&&field<=7)));
            bool slider=kind==Kind::NRField&&((field>=Intensity&&field<=History)||field==Scale||field==Passes);
            if(toggle){
                const bool on=value=="On";
                button({helpX-104,y+7,92,32},value,{kind,field,on?-1:1},on,Widget::Toggle);
            }else if(slider){
                const auto &s=settings[field];
                out.push_back({{controlX,y+7,controlWidth-66,32},value,true,false,{kind,field},Widget::Slider,choices.nr[Field(field)],s.minimum,s.maximum});
                item({helpX-69,y+13,58,20},value,Widget::Reading);
            }else{
                button({controlX,y+7,controlWidth,32},value,{Kind::OpenChoice,int(kind),field},false,Widget::Choice);
            }
            const int help=kind==Kind::Renderer?205:kind==Kind::Processing?206:kind==Kind::NRField?field:
                kind==Kind::Graphics?100+field:kind==Kind::CloudPreset?204:kind==Kind::Mode?200:
                kind==Kind::FGToggle||kind==Kind::FGProvider?201:kind==Kind::FGPacing?209:kind==Kind::FGCap?210:kind==Kind::MotionSource?211:202;
            button({helpX,y+7,32,32},"?",{Kind::Help,help});
        };
        const char *next="After restart",*nrTiming=liveNR?"Live":next;

        if(helpField>=0){
            item({24,top,width-150,30},helpField==300?"CHANGELOG":helpField<Count?utf8(settings[helpField].label):"SETTING HELP",Widget::Section);
            button({width-104,top,80,30},"Back",{Kind::Help,-1});
        }else if(!configured()){
            section("CONNECTION");
            item({28,cursor+5,width-56,20},"Link this simulator");
            item({28,cursor+33,width-56,60},"Enable in-sim integration for this X-Plane folder in the installation package, then restart X-Plane. Your saved settings will appear here.");
        }else if(dropdownOpen){
            const auto options=choiceOptions(dropdown);
            item({24,top,width-140,30},"CHOOSE AN OPTION",Widget::Section);
            button({width-104,top,80,30},"Back",{Kind::CloseChoice});
            dropdownScroll=std::clamp(dropdownScroll,0,maxDropdownScroll(height));
            for(size_t i=0;i<options.size();++i){
                const int y=top+44+int(i)*36-dropdownScroll;
                if(y>=top+40&&y+32<=bottom)button({24,y,width-48,32},options[i],{Kind::ChooseOption,int(i)},int(i)==choiceCurrent(dropdown));
            }
        }else if(tab==0){
            if(needsStartupRepair()){
                if(visible(cursor,34)){button({24,cursor,width-104,34},"Repair startup link",{Kind::RepairStartup});button({helpX,cursor,32,30},"?",{Kind::Help,208});}
                cursor+=44;
            }
            section("01   /   IMAGE PROCESSING",rendererConnected?"After restart unless marked live":"After restart");
            row("Image processor",xp_renderer::label(rendererChoice),Kind::Renderer,0,"");
            row("Resolution preset",choices.presetId.empty()?"Custom":utf8(graphics::presetById(choices.presetId).label),Kind::Preset,0,"");
            row("Enable FSR",choices.enabled?"On":"Off",Kind::Processing,0,rendererConnected?"Live":"");
            row("FSR mode",fsrNames[std::clamp(choices.mode,0,4)],Kind::Mode,0,"");
            row("Motion source",motionSourceNames[std::clamp(choices.motionSource,0,1)],Kind::MotionSource,0,"");
            section("02   /   FRAME GENERATION",next);
            row("Generate frames",choices.frameGeneration?"On":"Off",Kind::FGToggle,0,sessionFG?"Live":"");
            row("Frame generation method",fgNames[std::clamp(choices.lastFrameGeneration,1,4)],Kind::FGProvider,0,"");
            row("Frame pacing",xpfsr::pacing::profileNames[std::clamp(choices.fgPacing,0,2)],Kind::FGPacing,0,"");
            row("Frame rate cap",xpfsr::pacing::capNames[std::clamp(choices.fgCap,0,xpfsr::pacing::capCount-1)],Kind::FGCap,0,"");
            section("03   /   NEURAL RENDERING",nrTiming);
            row("Enable neural rendering",valueName(choices.nr,Enabled),Kind::NRField,Enabled,"");
            row("Where to use NR",valueName(choices.nr,Scope),Kind::NRField,Scope,"");
            row("When to apply NR",valueName(choices.nr,Order),Kind::NRField,Order,"");
            if(visible(cursor,68)){
                item({28,cursor+5,width-130,20},"NR speed and detail");
                button({helpX,cursor,32,30},"?",{Kind::Help,207});
                const int w=(width-60)/3;
                const char *labels[]={"Performance 33%","Balanced 50%","Quality 67%"};
                for(int i=0;i<3;++i)button({24+i*(w+6),cursor+32,w,32},labels[i],{Kind::Budget,i},selectedBudget()==i);
            }
        }else if(tab==1){
            section("01   /   MODEL LIBRARY");
            if(visible(cursor,32)){
                button({28,cursor,helpX-42,32},modelImportBusy?"Model import in progress...":"Import NR model DLL...",{Kind::ImportModel});
                button({helpX,cursor,32,32},"?",{Kind::Help,203});
            }
            if(visible(cursor+40,51))item({28,cursor+40,width-56,51},modelImportStatus,Widget::Muted);
            cursor+=modelSetupHeight;
            section("02   /   MODEL CONTROLS",nrTiming);
            for(int i=0;i<Count;++i)row(utf8(settings[i].label),valueName(choices.nr,Field(i)),Kind::NRField,i,"");
        }else if(tab==3){
            const auto o=choices.presetId.empty()?choices.manual:graphics::presetById(choices.presetId).settings;
            const char *msaa[]={"Off","2x MSAA (experimental)","4x MSAA (experimental)","8x MSAA (experimental)"};
            const char *scales[]={"Full - Native AA","Ultra quality","Quality - 1.5x","Balanced","Performance - 2x"};
            section("01   /   DISPLAY",next);
            row("MSAA",msaa[o.msaa],Kind::Graphics,0,"");
            row("FXAA",o.fxaa?"On":"Off",Kind::Graphics,1,"Native only");
            row("Input detail",scales[o.scale],Kind::Graphics,2,"");
            row("Output resolution",o.outputWidth?std::to_string(o.outputWidth)+" x "+std::to_string(o.outputHeight):"Keep current output",Kind::Graphics,3,"");
            row("Keep X-Plane menu size",choices.keepSimUISize?"On":"Off",Kind::Graphics,4,"When output changes");
            section("02   /   IMAGE PROTECTION",next);
            row("Reduce FSR 3 trails",choices.ghostReduction?"On":"Off",Kind::Graphics,5,"");
            row("Cockpit screen protection",choices.cockpitRestore?"On":"Off",Kind::Graphics,6,"");
            row("Check screen alignment",choices.alignmentTint?"On":"Off",Kind::Graphics,7,"");
            section("03   /   CLOUDS",next);
            const char *clouds[]={"Keep current cloud quality","Low","Medium","High","Maximum"};
            row("Cloud quality",clouds[o.cloudQuality+1],Kind::Graphics,8,"");
            row("Cloud clarity presets","Choose a clarity setup...",Kind::CloudPreset,0,"");
            if(visible(cursor+12,32))button({24,cursor+12,width-48,32},"Apply graphics after I close X-Plane",{Kind::ApplyGraphics},choices.manualPending);
        }else{
            if(visible(cursor,32))button({24,cursor,width-48,32},"Open log folder",{Kind::Logs});
            cursor+=42;
            if(visible(cursor,32))button({24,cursor,width-48,32},"Preview / send a Discord report",{Kind::Report});
            cursor+=42;
            if(visible(cursor,52))item({28,cursor,width-56,52},reportStatus,Widget::Muted);
            cursor+=60;
            section("01   /   FRAME COUNTERS");
            const int col=(width-52)/3;
            if(visible(cursor,62)){
                const char *labels[]={"Source recordings","Generated frames","Presented frames"};
                const uint64_t counts[]={telemetry.recordings,telemetry.fgGenerated,telemetry.displayedPresents};
                for(int i=0;i<3;++i){
                    item({28+i*col,cursor+4,col-12,20},labels[i],Widget::Muted);
                    item({28+i*col,cursor+27,col-12,20},std::to_string(counts[i]),Widget::Reading);
                }
            }
            cursor+=62;
            section("02   /   SESSION STATUS");
            auto info=[&](const std::string& title,const std::string& value,int blockHeight){
                if(visible(cursor,blockHeight)){
                    item({28,cursor+3,width-56,20},title,Widget::Muted);
                    item({28,cursor+24,width-56,blockHeight-24},value);
                }
                cursor+=blockHeight;
            };
            info("Renderer",activity,61);
            info("Image size","Input "+std::to_string(telemetry.renderWidth)+" x "+std::to_string(telemetry.renderHeight)+"   /   Output "+std::to_string(telemetry.outputWidth)+" x "+std::to_string(telemetry.outputHeight),48);
            info("Frame generation",frameGenerationStatus(),75);
            info("Neural rendering",nrStatus,75);
            info("Log location","X-Plane / Output / FSR-Logs. A fresh copy is also saved after exit.",48);
        }
        out.erase(std::remove_if(out.begin(),out.end(),[&](const Item&i){
            const auto &r=i.rect;
            return r.w<=0||r.h<=0||r.x<0||r.y<0||r.x+r.w>width||r.y+r.h>height||(r.y>=top&&r.y+r.h>bottom);
        }),out.end());
        out.push_back({footerNotice(width,height),notice.empty()?(configured()?"Your choices save automatically.":"Setup is not linked. Your saved choices are kept."):notice});
        if(helpField<0){
            const char* hints[]={"Select ? for help. Queue graphics changes on Image quality before exiting.","Import your model first. Use ? beside a setting for its range and effect.","Counters come from this session. Frame generation pauses while this panel is open.","Use Apply graphics, then close X-Plane normally and restart."};
            out.push_back({footerGuidance(width,height),dropdownOpen?"Choose a value to save it. Back keeps your current selection.":configured()?hints[tab]:"Use the installation package to connect this simulator.",false,false,{Kind::Help,-1},Widget::Muted});
        }
        return out;
    }
};
} // namespace insim
