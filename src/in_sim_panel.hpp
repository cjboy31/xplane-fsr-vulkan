// SPDX-License-Identifier: MIT
#pragma once
#include "settings_transaction.hpp"
#include "shared.hpp"
#include "framegen_policy.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
namespace insim {
using namespace xpfsr::nr;
inline std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline constexpr const char *fsrNames[]={"Native / processing bypass","FSR 3 Native AA","FSR 3 Upscaling","FSR 4 Native AA","FSR 4 Upscaling"};
inline constexpr const char *fgNames[]={"Off","FSR 3","FSR 4","FSR 4 synchronized","FSR 3 synchronized"};
inline std::string valueName(const Controls &c,Field f){
    int value=c[f];
    if(f==Enabled||f==Mask||f==Grade||f==AllowFG)return value?"On":"Off";
    if(f==Scope)return value?"Full scene":"External only";
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
enum class Kind {Tab,Processing,Mode,FrameGen,NRField,Budget,Help,Close,Preset,Graphics,ApplyGraphics,OpenChoice,ChooseOption,CloseChoice,FGToggle,FGProvider,ImportModel,CloudPreset};
inline constexpr int contentTop=160, footerHeight=120, modelSetupHeight=132;
inline Rect footerNotice(int width,int height){return {24,height-112,width-48,34};}
inline Rect footerGuidance(int width,int height){return {24,height-62,width-48,51};}
enum class Widget {Label,Button,Toggle,Slider,Choice};
struct Action {Kind kind;int field{},delta{};};
struct Item {Rect rect;std::string text;bool button{},selected{};Action action{Kind::Help,-1,0};Widget widget{Widget::Label};int value{},minimum{},maximum{100};};
class Panel {
    std::filesystem::path file;
    ControlChannel *channel{};
    std::function<void(const launcher::Preferences&)> publish;
    std::function<void()> queueGraphics;
    std::function<void()> importModel;
    bool liveNR{},sessionFG{},rendererConnected{true};
    std::vector<const graphics::Preset*> displayPresets;
    uint64_t previousRecords{},lastProgress{};bool sawStatus{};
public:
    launcher::Preferences choices;
    app::Telemetry telemetry{};
    int tab{},scroll{},helpField{-1};std::string notice,activity="PLUGIN LOADED - renderer not confirmed",nrStatus="NR status not available";
    bool dropdownOpen{};Action dropdown{Kind::Mode};int dropdownScroll{};
    bool modelImportBusy{};std::string modelImportStatus="Import your own nvngx_dlssnr.dll 310.9.16 or 310.8.0. Models are not included.";
    Panel()=default;
    void bind(const std::filesystem::path &settings,ControlChannel *controls,bool canNR,bool hasFG,const std::function<void(const launcher::Preferences&)> &onPublish={}){
        file=settings;channel=controls;liveNR=canNR;sessionFG=hasFG;publish=onPublish;refresh();
        displayPresets=graphics::availableResolutionPresets([](int w,int h){return graphics::supportsOutput(w,h);});
    }
    bool configured()const{return !file.empty();}
    bool rendererLinked()const{return configured()&&rendererConnected;}
    void configurationOnly(const std::string &reason,bool activationBypassed=false){rendererConnected=false;liveNR=false;sessionFG=false;activity=activationBypassed?"RENDERER BYPASSED - see startup reason below":"RENDERER NOT LOADED - next-session settings available";nrStatus="Not running in this session";notice=reason;}
    void graphicsSupport(std::function<void()> callback){queueGraphics=std::move(callback);}
    void modelSupport(std::function<void()> callback){importModel=std::move(callback);}
    void refresh(){if(configured())choices=launcher::load(file);}
    std::vector<std::string> choiceOptions(Action a)const{
        if(a.kind==Kind::NRField&&(a.field<0||a.field>=Count))return {};
        if(a.kind==Kind::Mode)return {std::begin(fsrNames),std::end(fsrNames)};
        if(a.kind==Kind::FrameGen)return {std::begin(fgNames),std::end(fgNames)};
        if(a.kind==Kind::FGProvider)return {std::begin(fgNames)+1,std::end(fgNames)};
        if(a.kind==Kind::CloudPreset)return {"Keep current choices","Cloud clarity - Ultra Quality source","Cloud clarity - Native AA source (highest cost)"};
        if(a.kind==Kind::Preset){std::vector<std::string> names{"Custom"};for(auto p:displayPresets)names.push_back(utf8(p->label));return names;}
        if(a.kind==Kind::NRField){std::vector<std::string> result;auto c=choices.nr;for(int i=settings[a.field].minimum;i<=settings[a.field].maximum;++i){c[Field(a.field)]=i;result.push_back(valueName(c,Field(a.field)));}return result;}
        if(a.kind==Kind::Graphics){if(a.field==0)return {"Off","2x MSAA (experimental)","4x MSAA (experimental)","8x MSAA (experimental)"};if(a.field==8)return {"Keep current cloud quality","Low","Medium","High","Maximum"};if(a.field==2)return {"Full - Native AA","Ultra quality","Quality - 1.5x","Balanced","Performance - 2x"};if(a.field==3){std::vector<std::string> result;for(auto o:graphics::outputChoices)result.push_back(utf8(o.label));return result;}}
        return {};
    }
    int choiceCurrent(Action a)const{
        if(a.kind==Kind::Mode)return choices.mode;if(a.kind==Kind::FrameGen)return choices.frameGeneration;
        if(a.kind==Kind::FGProvider)return choices.lastFrameGeneration-1;
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
        else activity="PLUGIN LOADED - waiting for advancing renderer frames";
    }
    void persist(launcher::Preferences proposed){
        if(!configured())throw std::runtime_error("In-sim setup is not linked. Run setup and select this X-Plane installation.");
        if(proposed.nr[Enabled]&&!liveNR&&!proposed.mode)throw std::runtime_error("Select an FSR AA/upscaling profile for the next session before enabling NR.");
        if(proposed.nr[Enabled]&&sessionFG&&!proposed.nr[AllowFG])throw std::runtime_error("Enable Allow NR + frame generation first, or use an FG-Off session.");
        choices=launcher::transactChoices(file,choices,proposed,[&](const auto &merged){if(channel)channel->write(merged.nr);if(publish)publish(merged);});
        notice=liveNR?"Saved. NR/processing controls are live; FSR and FG providers apply next session.":"Saved for the next session. This session has no live NR renderer; correct/queue graphics and restart using your usual launcher.";
    }
    void act(Action action,bool refreshFirst=true){
        if(action.kind==Kind::Tab){tab=std::clamp(action.field,0,3);scroll=0;helpField=-1;dropdownOpen=false;return;}
        if(action.kind==Kind::Help){helpField=action.field;return;}
        if(action.kind==Kind::OpenChoice){dropdown={Kind(action.field),action.delta};dropdownOpen=true;dropdownScroll=0;helpField=-1;return;}
        if(action.kind==Kind::CloseChoice){dropdownOpen=false;return;}
        if(action.kind==Kind::ChooseOption){refresh();auto options=choiceOptions(dropdown);if(action.field<0||action.field>=int(options.size()))throw std::runtime_error("Invalid dropdown selection");auto selected=dropdown;selected.delta=action.field-choiceCurrent(dropdown);if(selected.kind==Kind::NRField)selected.delta=action.field+settings[selected.field].minimum-choices.nr[Field(selected.field)];dropdownOpen=false;act(selected,false);return;}
        if(action.kind==Kind::ApplyGraphics){if(!queueGraphics)throw std::runtime_error("Graphics support is unavailable; reinstall the complete in-sim package.");queueGraphics();notice="Queued. Graphics will apply after you close X-Plane normally. Your current flight continues.";return;}
        if(action.kind==Kind::ImportModel){
            if(modelImportBusy){notice="A model import is already open. Complete or cancel its file selector, then wait for the result.";return;}
            if(!importModel)throw std::runtime_error("Model import support is unavailable; reinstall the complete in-sim package.");
            importModel();return;
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
        if(action.kind==Kind::Budget)notice=budgetSelectionMessage(action.field);
        if(action.kind==Kind::CloudPreset)notice="Cloud clarity setup saved: Maximum clouds with "+std::string(action.delta==1?"Ultra Quality input.":"full input and Native AA.")+" Select Apply graphics after exit, then restart. Higher source resolution costs GPU time; distant cloud detail remains limited by X-Plane's cloud pass.";
        if(action.kind==Kind::Graphics&&action.field==8)notice="Cloud quality saved for next session. Select Apply graphics after exit. This controls X-Plane's cloud quality; FSR/FG/NR choices are retained.";
    }
    int maxScroll(int height)const{return tab==2?0:std::max(0,(tab==1?Count*64+modelSetupHeight:tab==3?642:510)-std::max(1,height-contentTop-footerHeight));}
    int maxDropdownScroll(int height)const{return std::max(0,int(choiceOptions(dropdown).size())*36-std::max(1,height-contentTop-footerHeight-68));}
    bool wheel(int amount,int height){auto &position=dropdownOpen?dropdownScroll:scroll;int next=std::clamp(position+amount,0,dropdownOpen?maxDropdownScroll(height):maxScroll(height));if(next==position)return false;position=next;return true;}
    std::string help()const{
        if(!configured())return "Rendering controls are unavailable until this simulator is linked to a saved installation. No selections have been changed. Enable in-sim integration for this X-Plane folder, then restart using your usual launcher.";
        if(helpField>=0&&helpField<Count)return utf8(settings[helpField].help);
        if(helpField==100)return "MSAA samples geometry edges before temporal reconstruction. It increases GPU and memory cost. MSAA + FSR is experimental; start Off and compare the same scene. Select Apply after exit for graphics changes.";
        if(helpField==101)return "FXAA smooths the final native image, including text, and can soften detail. It must be Off with FSR Native AA/upscaling. Use it only with FSR mode Off.";
        if(helpField==102)return "Full input scale is required for Native AA. Lower input scales are for FSR upscaling. Quality at 4K output uses a 1440p input. Lower scales trade source detail for performance.";
        if(helpField==103)return "Output resolution controls the displayed image. Keep current leaves window mode and display geometry unchanged. Choose an advertised resolution, then Apply after exit. This does not add 4K pixels to a display that cannot present 4K.";
        if(helpField==104)return "Preserves the logical size of X-Plane's interface when this plugin changes output resolution. Font preferences are retained; readability limits apply. Ordinary launches preserve existing display/UI preferences.";
        if(helpField==105)return "Conservative FSR 3 history rejection can reduce trails but increase shimmer. This affects FSR 3, not FSR 4, and applies next session.";
        if(helpField==106||helpField==107)return "Cockpit-screen restoration currently targets the default Cirrus SR22 PFD/MFD. It is not a universal aircraft fix. FG must be Off. Alignment tint briefly marks accepted screen pixels; disable it after checking. Applies next session.";
        if(helpField==108)return "X-Plane's own cloud quality slider: Keep current, Low, Medium, High or Maximum. Higher quality can cost GPU time. Maximum still uses a reduced-resolution cloud pass in 12.4.4; FSR cannot restore all absent source detail. Applies after normal exit with a backup. No weather, cloud coverage or add-on settings are changed.";
        if(helpField==200)return "Native AA reconstructs at full input resolution for edge stability. Upscaling reconstructs a larger output from lower input resolution. Match the input scale in Image quality and keep FXAA Off. Provider changes require a new simulator session.";
        if(helpField==201)return "Frame generation inserts intermediate frames. It does not improve simulation rate or repair a CPU bottleneck. Start with stable base FPS. Provider changes apply next session. FG pauses while setup is open; capture-hook conflicts may require the optional desktop launch path.";
        if(helpField==202)return "A resolution preset selects output, input scale and FSR provider together. It sets MSAA, FXAA and frame generation Off. Queue Apply after exit to use its graphics settings on your next launch. Custom graphics edits retain your selected FSR/FG providers.";
        if(helpField==203)return "Select your own nvngx_dlssnr.dll 310.9.16 (Small + Original) or 310.8.0 (Original). The background importer reads it without loading or executing it, and verifies supported model hashes. It does not copy the NVIDIA DLL into the application. A first import can finish during your flight; turn NR Off then On to retry. Existing differing models are protected while X-Plane is running.";
        if(helpField==204)return "Cloud clarity setups increase the scene input resolution and select Maximum cloud quality. Ultra Quality retains upscaling; Native AA uses full scene input and can cost much more GPU time. Your FSR provider family, FG, NR, MSAA and output choices are retained. This may reduce blockiness but cannot guarantee a cloud-engine fix. Apply after exit and compare the same weather/view. X-Plane's core rendering table is not modified.";
        if(tab==1)return "Import your own model DLL here. First-time import runs in the background; after success turn NR Off then On. Live NR sliders save automatically. FSR continues during model preparation; NR requires supported AMD RDNA4 FP8 hardware.";
        return tab==3?"Choose your graphics settings, then select Apply after exit. Close X-Plane normally; the background component applies only these settings with a backup. Restart using your usual launcher. FSR/FG choices are retained for custom edits. FXAA is native-only; MSAA + FSR is experimental.":"Live NR and processing controls save automatically. Presets/graphics and FSR/FG providers need a new session. Queue changed graphics on the Image quality tab before exiting. FG pauses while this panel is visible. The desktop app is optional.";
    }
    std::vector<Item> view(int width,int height){
        std::vector<Item> out;
        auto text=[&](int x,int y,int w,const std::string &s){out.push_back({{x,y,w,20},s});};
        auto button=[&](Rect r,const std::string&s,Action action,bool selected=false){out.push_back({r,s,true,selected,action,Widget::Button});};
        text(24,20,width-152,"FSR 3/4 + FRAME GEN");text(24,48,width-48,activity);
        text(24,64,width-48,"NR: "+nrStatus);
        button({width-112,12,88,30},"Close",{Kind::Close});
        int tabWidth=(width-72)/4;const char *tabs[]={"Rendering","Neural rendering","Diagnostics","Image quality"};for(int i=0;i<4;++i)button({24+i*(tabWidth+8),108,tabWidth,32},tabs[i],{Kind::Tab,i},tab==i);
        int top=contentTop,bottom=height-footerHeight;scroll=std::clamp(scroll,0,maxScroll(height));int origin=top-scroll;
        auto row=[&](int y,const std::string &label,const std::string &value,Kind kind,int field){
            if(y<top||y+54>bottom)return;
            const bool toggle=kind==Kind::NRField&&(field==Enabled||field==Mask||field==Grade||field==AllowFG)||kind==Kind::Graphics&&(field==1||(field>=4&&field<=7))||kind==Kind::FGToggle;
            const bool slider=kind==Kind::NRField&&((field>=Intensity&&field<=History)||field==Scale||field==Passes);
            if(toggle){text(24,y+8,width-220,label);bool on=kind==Kind::NRField?choices.nr[Field(field)]!=0:value=="On";out.push_back({{width-178,y+2,98,34},on?"On":"Off",true,on,{kind,field,on?-1:1},Widget::Toggle});}
            else if(slider){text(24,y,width-240,label);text(width-190,y,100,value);auto &s=settings[field];out.push_back({{24,y+24,width-112,28},value,true,false,{kind,field},Widget::Slider,choices.nr[Field(field)],s.minimum,s.maximum});}
            else{text(24,y,width-120,label);out.push_back({{24,y+22,width-104,30},value,true,false,{Kind::OpenChoice,int(kind),field},Widget::Choice});}
            const int help=kind==Kind::NRField?field:kind==Kind::Graphics?100+field:kind==Kind::CloudPreset?204:kind==Kind::Mode?200:kind==Kind::FrameGen||kind==Kind::FGToggle||kind==Kind::FGProvider?201:202;
            button({width-66,y+4,36,30},"?",{Kind::Help,help});
        };
        if(!configured()){
            text(24,top,width-48,"Simulator connection required");
            text(24,top+36,width-48,"The renderer did not supply a valid settings link, and no matching installed profile was found. Enable in-sim integration for this simulator, then restart. The desktop app remains optional.");out.back().rect.h=60;
        }else if(dropdownOpen){
            auto options=choiceOptions(dropdown);text(24,top,width-140,"Choose an option");button({width-112,top-4,88,30},"Back",{Kind::CloseChoice});dropdownScroll=std::clamp(dropdownScroll,0,maxDropdownScroll(height));
            for(size_t i=0;i<options.size();++i){int y=top+48+int(i)*36-dropdownScroll;if(y>=top+44&&y+32<=bottom)button({24,y,width-48,32},options[i],{Kind::ChooseOption,int(i)},int(i)==choiceCurrent(dropdown));}
        }else if(helpField>=0){
            text(24,top,width-140,helpField<Count?utf8(settings[helpField].label):"Setting guidance");button({width-112,top-4,88,30},"Back",{Kind::Help,-1});
        }else if(tab==0){
            row(origin,"Resolution preset - queue graphics before exiting",choices.presetId.empty()?"Custom":utf8(graphics::presetById(choices.presetId).label),Kind::Preset,0);
            if(origin+56>=top){button({24,origin+56,width-48,38},rendererConnected?"Enable FSR processing - live":"Enable FSR processing - next session",{Kind::Processing,0,choices.enabled?-1:1},choices.enabled);out.back().widget=Widget::Toggle;}
            row(origin+112,"FSR / AA - next session",fsrNames[std::clamp(choices.mode,0,4)],Kind::Mode,0);
            row(origin+168,"Enable frame generation - next session",choices.frameGeneration?"On":"Off",Kind::FGToggle,0);
            row(origin+224,"Frame generation provider",fgNames[std::clamp(choices.lastFrameGeneration,1,4)],Kind::FGProvider,0);
            row(origin+280,liveNR?"Neural rendering - live":"Neural rendering - next session",valueName(choices.nr,Enabled),Kind::NRField,Enabled);
            row(origin+336,liveNR?"Scene scope - live":"Scene scope - next session",valueName(choices.nr,Scope),Kind::NRField,Scope);
            row(origin+392,liveNR?"Processing order - live":"Processing order - next session",valueName(choices.nr,Order),Kind::NRField,Order);
            if(origin+478>=top&&origin+510<=bottom){if(origin+452>=top)text(24,origin+452,width-48,liveNR?"NR work budget - live, one pass":"NR work budget - next session, one pass");int w=(width-64)/3;const char *labels[]={"Performance 33%","Balanced 50%","Quality 67%"};int selected=selectedBudget();for(int i=0;i<3;++i)button({24+i*(w+8),origin+478,w,32},std::string(labels[i])+(selected==i?" [Selected]":""),{Kind::Budget,i},selected==i);}
        }else if(tab==1){
            if(origin>=top&&origin+32<=bottom){button({24,origin,width-104,32},modelImportBusy?"Model import in progress...":"Import NR model DLL...",{Kind::ImportModel});button({width-66,origin,36,30},"?",{Kind::Help,203});}
            if(origin+44>=top&&origin+100<=bottom){text(24,origin+44,width-48,modelImportStatus);out.back().rect.h=56;}
            scroll=std::clamp(scroll,0,maxScroll(height));for(int i=0;i<Count;++i){int y=top+modelSetupHeight+i*64-scroll;if(y<top||y+54>bottom)continue;row(y,utf8(settings[i].label),valueName(choices.nr,Field(i)),Kind::NRField,i);}
        }else if(tab==3){
            auto o=choices.presetId.empty()?choices.manual:graphics::presetById(choices.presetId).settings;
            const char *msaa[]={"Off","2x MSAA (experimental)","4x MSAA (experimental)","8x MSAA (experimental)"};const char *scales[]={"Full - Native AA","Ultra quality","Quality - 1.5x","Balanced","Performance - 2x"};
            row(origin,"MSAA",msaa[o.msaa],Kind::Graphics,0);row(origin+56,"FXAA - native only",o.fxaa?"On":"Off",Kind::Graphics,1);
            row(origin+112,"Input scale",scales[o.scale],Kind::Graphics,2);row(origin+168,"Output resolution",o.outputWidth?std::to_string(o.outputWidth)+" x "+std::to_string(o.outputHeight):"Keep current output",Kind::Graphics,3);
            row(origin+224,"Keep X-Plane UI size when output changes",choices.keepSimUISize?"On":"Off",Kind::Graphics,4);
            row(origin+280,"Reduce FSR 3 history trails - next session",choices.ghostReduction?"On":"Off",Kind::Graphics,5);
            row(origin+336,"Cockpit-screen restore - next session, FG Off",choices.cockpitRestore?"On":"Off",Kind::Graphics,6);
            row(origin+392,"Screen alignment tint - next session",choices.alignmentTint?"On":"Off",Kind::Graphics,7);
            const char *clouds[]={"Keep current cloud quality","Low","Medium","High","Maximum"};
            row(origin+448,"Cloud quality - next session",clouds[o.cloudQuality+1],Kind::Graphics,8);
            row(origin+504,"Cloud clarity setups", "Choose a clarity setup...",Kind::CloudPreset,0);
            if(origin+580>=top&&origin+612<=bottom)button({24,origin+580,width-48,32},"Apply graphics after I close X-Plane",{Kind::ApplyGraphics},choices.manualPending);
        }else{
            text(24,top,width-48,"Renderer activity requires advancing frames; loading this plugin alone is not proof.");
            text(24,top+34,width-48,"FSR source recordings: "+std::to_string(telemetry.recordings));
            text(24,top+64,width-48,"Input "+std::to_string(telemetry.renderWidth)+" x "+std::to_string(telemetry.renderHeight)+"  /  Output "+std::to_string(telemetry.outputWidth)+" x "+std::to_string(telemetry.outputHeight));
            text(24,top+94,width-48,"Generated frames: "+std::to_string(telemetry.fgGenerated)+"  /  Presented: "+std::to_string(telemetry.displayedPresents));
            text(24,top+128,width-48,"NR: "+nrStatus);text(24,top+184,width-48,"Current-session logs are in the installation's logs folder.");
        }
        // Only fully visible controls are clickable. Long labels/help are clipped
        // and wrapped by the XPLM drawing adapter inside the panel's content area.
        out.erase(std::remove_if(out.begin(),out.end(),[&](const Item&i){return i.rect.x<0||i.rect.y<0||i.rect.x+i.rect.w>width||i.rect.y+i.rect.h>height||(i.rect.y>=top&&i.rect.y+i.rect.h>bottom);}),out.end());
        out.push_back({footerNotice(width,height),notice.empty()?"Saved automatically. The desktop app is an optional secondary interface.":notice});
        if(helpField<0)out.push_back({footerGuidance(width,height),dropdownOpen?"Choose an option. Scroll to reveal more choices; Back returns without changing the selection.":help()});
        return out;
    }
};
} // namespace insim
