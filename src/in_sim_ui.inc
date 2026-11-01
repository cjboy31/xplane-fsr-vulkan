// SPDX-License-Identifier: MIT
// Original XPLM UI. It never requests camera/joystick control or global keys.
#include "in_sim_panel.hpp"
#include "panel_presence.hpp"
#include "nr_model_import.hpp"
#include "session_logs.hpp"
#include "in_sim_binding.hpp"
#include "session_export.hpp"
#include <shellapi.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <GL/gl.h>
#include <cmath>
#include <map>
static XPLMWindowID setupWindow{};
static insim::Panel setupPanel;
static xpfsr::nr::ControlChannel setupNR;
static app::PanelPresenceChannel setupPresence;
static ULONGLONG setupLastPoll{};
static std::filesystem::path setupRoot;
static std::wstring setupToken;
static HANDLE setupGraphicsJob{};
static HANDLE setupImportJob{};
static HANDLE setupLogJob{};
static HANDLE setupRepairJob{};
static std::wstring setupLogToken;
static std::string setupLastBindingReport;
static insim::ConfigurationSession setupConfigurationSession;
static std::wstring setupImportToken;
static int setupDragField=-1;
static int setupDragPreview{};
static ULONGLONG setupLastDrag{};
static std::wstring setupEnvironment(const wchar_t *key){wchar_t value[32768]{};auto n=GetEnvironmentVariableW(key,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring{};}
static bool setupVisible(){return setupWindow&&XPLMGetWindowIsVisible(setupWindow);}
static void setupHide(){setupDragField=-1;setupPanel.dropdownOpen=false;if(setupWindow){XPLMSetWindowIsVisible(setupWindow,0);if(XPLMHasKeyboardFocus(setupWindow))XPLMTakeKeyboardFocus(nullptr);}setupPresence.setVisible(false);}
static void setupQueueGraphics(){
    if(setupGraphicsJob){if(WaitForSingleObject(setupGraphicsJob,0)==WAIT_TIMEOUT)return;CloseHandle(setupGraphicsJob);setupGraphicsJob=nullptr;}
    if(setupToken.size()!=32||setupToken.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos)throw std::runtime_error("No current renderer session; enable integration and restart before queuing graphics");
    if(!launcher::load(setupRoot/L"settings.ini").manualPending)throw std::runtime_error("There are no pending graphics changes");
    auto helper=setupRoot/L"IntegrationSetup.exe";if(!std::filesystem::is_regular_file(helper))throw std::runtime_error("The graphics support component is missing; reinstall the complete package");
    if(!launcher::writeSetting(L"InSim",L"GraphicsQueued",setupToken.c_str(),(setupRoot/L"settings.ini").c_str()))throw std::runtime_error("Cannot queue graphics changes");
    auto command=L"\""+helper.wstring()+L"\" --apply-graphics-after "+std::to_wstring(GetCurrentProcessId())+L" "+setupToken;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,setupRoot.c_str(),&startup,&process))throw std::runtime_error("Could not start the graphics support component");
    CloseHandle(process.hThread);setupGraphicsJob=process.hProcess;
}
static void setupImportModel(){
    if(setupImportJob&&WaitForSingleObject(setupImportJob,0)==WAIT_TIMEOUT)throw std::runtime_error("Wait for the current model import to finish");
    if(setupImportJob){CloseHandle(setupImportJob);setupImportJob=nullptr;}
    if(!xpfsr::nr::validImportToken(setupToken))throw std::runtime_error("No current simulator session is linked to model import");
    const auto helper=setupRoot/L"IntegrationSetup.exe";
    if(!std::filesystem::is_regular_file(helper))throw std::runtime_error("Model setup component is missing; reinstall the complete package");
    auto id=session::id();setupImportToken=std::wstring(id.begin(),id.end());
    auto command=L"\""+helper.wstring()+L"\" --import-nr "+std::to_wstring(GetCurrentProcessId())+L" "+setupToken+L" "+setupImportToken;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,setupRoot.c_str(),&startup,&process))throw std::runtime_error("Could not open the model import component");
    CloseHandle(process.hThread);setupImportJob=process.hProcess;setupPanel.modelImportBusy=true;
    setupPanel.modelImportStatus="Opening the file selector. Select your own supported nvngx_dlssnr.dll.";
    setupPanel.notice="NR model import runs separately; FSR continues. Your rendering selections are retained.";
}
static void setupOpenUpdater(){
    const auto exe=setupRoot/L"XPlaneFSR24.exe";
    if(!std::filesystem::is_regular_file(exe))throw std::runtime_error("The plugin updater is missing. Reinstall the complete package.");
    std::wstring command=L"\""+exe.wstring()+L"\" --updates";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,setupRoot.c_str(),&startup,&process))throw std::runtime_error("Could not open the plugin updater.");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
static void setupRepairStartup(){
    if(setupRepairJob&&WaitForSingleObject(setupRepairJob,0)==WAIT_TIMEOUT)throw std::runtime_error("Startup repair is already running. Wait for its result.");
    if(setupRepairJob){CloseHandle(setupRepairJob);setupRepairJob=nullptr;}
    const auto helper=setupRoot/L"IntegrationSetup.exe";
    if(!std::filesystem::is_regular_file(helper))throw std::runtime_error("The repair component is missing. Reinstall the complete package.");
    auto command=L"\""+helper.wstring()+L"\" --repair-registration";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,setupRoot.c_str(),&startup,&process))throw std::runtime_error("Could not start the repair component.");
    CloseHandle(process.hThread);setupRepairJob=process.hProcess;
}
static void setupPollRepair(){
    if(!setupRepairJob||WaitForSingleObject(setupRepairJob,0)==WAIT_TIMEOUT)return;
    DWORD code=1;GetExitCodeProcess(setupRepairJob,&code);CloseHandle(setupRepairJob);setupRepairJob=nullptr;
    setupPanel.notice=code?"Startup repair could not finish. Check logs/integration-setup.txt in the installation folder, or reinstall this mod for this X-Plane folder.":"Startup link repaired. Close X-Plane normally, then reopen using your usual shortcut. Your settings are kept.";
}
static void setupOpenLogs(){
    const auto sim=std::filesystem::path(setupPanel.choices.simulator);
    if(sim.empty()||!app::validSessionToken(setupToken))throw std::runtime_error("No simulator session is linked to this log.");
    if(!app::exportSessionLog(setupRoot,sim,setupToken,STILL_ACTIVE))throw std::runtime_error("The current session log is not available yet.");
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",app::userLogDirectory(sim).c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        throw std::runtime_error("Open Output / FSR-Logs in your X-Plane folder.");
}
static void setupMonitorLogs(){
    if(!app::validSessionToken(setupToken)||setupLogToken==setupToken)return;
    const auto helper=setupRoot/L"IntegrationSetup.exe";
    if(!std::filesystem::is_regular_file(helper)){setupPanel.notice="Session log helper is missing. Use Diagnostics > Open log folder before exiting, then reinstall the complete package.";return;}
    auto command=L"\""+helper.wstring()+L"\" --finish-session "+std::to_wstring(GetCurrentProcessId())+L" "+setupToken;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,setupRoot.c_str(),&startup,&process)){setupPanel.notice="Session log export could not start. Use Diagnostics > Open log folder before exiting.";return;}
    CloseHandle(process.hThread);if(setupLogJob)CloseHandle(setupLogJob);setupLogJob=process.hProcess;setupLogToken=setupToken;
}
static void setupOpenReports(){
    if(!app::validSessionToken(setupToken))throw std::runtime_error("No current session is linked to reporting");
    const auto helper=setupRoot/L"IntegrationSetup.exe";
    if(!std::filesystem::is_regular_file(helper))throw std::runtime_error("Report helper is missing. Reinstall the complete package.");
    auto command=L"\""+helper.wstring()+L"\" --report-ui "+std::to_wstring(GetCurrentProcessId())+L" "+setupToken;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,setupRoot.c_str(),&startup,&process))throw std::runtime_error("Report preview could not open. Use the log folder for now.");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
static void setupPollImport(){
    if(!setupImportJob)return;
    auto status=xpfsr::nr::readModelImportProgress(setupRoot,setupImportToken);
    if(!status.message.empty())setupPanel.modelImportStatus=status.message;
    if(WaitForSingleObject(setupImportJob,0)==WAIT_TIMEOUT)return;
    DWORD code=1;GetExitCodeProcess(setupImportJob,&code);CloseHandle(setupImportJob);setupImportJob=nullptr;setupPanel.modelImportBusy=false;
    if(status.phase!="success"&&status.phase!="cancelled"&&status.phase!="error")setupPanel.modelImportStatus=code?"Model import could not finish. Reinstall the complete package if the setup component is missing.":"Import ended without a verified result. Try again.";
    setupPanel.notice=setupPanel.modelImportStatus;
}
static void setupBind(){
    wchar_t manifest[32768]{};DWORD manifestBytes=sizeof(manifest);
    const auto registryResult=RegGetValueW(HKEY_CURRENT_USER,L"SOFTWARE\\XPFSR24\\InSim",L"Manifest",RRF_RT_REG_SZ,nullptr,manifest,&manifestBytes);
    auto registered=registryResult==ERROR_SUCCESS?std::filesystem::path(manifest):std::filesystem::path{};
    wchar_t executable[32768]{};const auto executableLength=GetModuleFileNameW(nullptr,executable,32768);
    auto binding=insim::resolveSettings(executable,setupEnvironment(L"XPFSR24_SETTINGS"),setupEnvironment(L"XPFSR24_RUNTIME"),registered);
    std::string report="XPFSR24 setup binding: pid="+std::to_string(GetCurrentProcessId())+" registry_result="+std::to_string(registryResult)+" executable_length="+std::to_string(executableLength)+" result="+(binding.file.empty()?binding.reason:binding.reason.empty()?"linked":"linked: "+binding.reason)+" executable="+insim::utf8(executable)+" manifest="+insim::utf8(registered.wstring());
    if(report!=setupLastBindingReport){XPLMDebugString((report+"\n").c_str());setupLastBindingReport=std::move(report);}
    if(binding.file.empty()){setupPanel.notice=binding.reason+". Setup has not changed any selections.";return;}
    auto file=binding.file;
    setupRoot=file.parent_path();setupToken=setupEnvironment(L"XPFSR24_SESSION");
    if(setupToken.empty())setupToken=setupEnvironment(L"XPFSR24_CONFIG_SESSION");
    const auto name=setupEnvironment(L"XPFSR24_NR_CONTROL");
    bool tokenValid=setupToken.size()==32&&std::all_of(setupToken.begin(),setupToken.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});
    auto mode=setupEnvironment(L"XPFSR24_MODE");xpfsr::nr::Controls live;
    const bool connected=tokenValid&&GetModuleHandleW(L"XPFSR24Layer.dll")&&(mode==L"upscale"||mode==L"native-aa");
    if(!connected&&!tokenValid){
        // Own the same per-installation log lease as automatic activation. A
        // control-only session must get fresh diagnostics without erasing a
        // different simulator session's active log.
        const auto owner=xp_renderer::environment("XP_RENDERER_OWNER"),selection=xp_renderer::environment("XP_RENDERER_SESSION_CHOICE");
        const std::string reason=owner=="native_aa"?"Native AA owns this session. FSR, NR and frame generation are intentionally inactive; both mods' saved settings are retained.":
            selection=="off"?"Original simulator selected for this session. Both mod renderers are intentionally inactive; saved settings are retained.":"";
        auto token=setupConfigurationSession.open(setupRoot,reason);
        setupToken=std::wstring(token.begin(),token.end());SetEnvironmentVariableW(L"XPFSR24_CONFIG_SESSION",setupToken.c_str());
    }
    bool canNR=connected&&name==L"Local\\XPFSR24-NR-v4-"+setupToken&&setupNR.open(name.c_str())&&setupNR.read(live);
    auto fg=setupEnvironment(L"XPFSR24_FG");
    setupPanel.bind(file,canNR?&setupNR:nullptr,canNR,connected&&!fg.empty()&&setupEnvironment(L"XPFSR24_FG_READY")==L"1",[](const launcher::Preferences&p){
        if(shared)InterlockedExchange(&shared->enabled,p.enabled?1:0);
        auto path=setupEnvironment(L"XPFSR24_CONTROL");
        if(!path.empty()&&std::filesystem::weakly_canonical(path)==std::filesystem::weakly_canonical(setupRoot/L"logs/renderer-control.txt")){
            std::ofstream out{std::filesystem::path(path)};out<<(p.enabled?'1':'0');if(!out)throw std::runtime_error("Saved, but the live processing control could not be updated");}
        auto fgPath=setupEnvironment(L"XPFSR24_FG_CONTROL");
        if(!setupEnvironment(L"XPFSR24_FG").empty()&&!fgPath.empty()&&std::filesystem::weakly_canonical(fgPath)==std::filesystem::weakly_canonical(setupRoot/L"logs/renderer-fg-control.txt")){
            std::ofstream out{std::filesystem::path(fgPath)};out<<(p.frameGeneration?'1':'0');if(!out)throw std::runtime_error("Saved, but the live frame-generation switch could not be updated");}
    });
    setupPanel.graphicsSupport(setupQueueGraphics);
    setupPanel.modelSupport(setupImportModel);
    setupPanel.updateSupport(setupOpenUpdater);
    setupPanel.logSupport(setupOpenLogs);
    setupPanel.reportSupport(setupOpenReports);
    const auto selected=xp_renderer::environment("XP_RENDERER_SESSION_CHOICE"),owner=xp_renderer::environment("XP_RENDERER_OWNER");
    setupPanel.repairSupport(!connected&&selected!="off"&&owner!="native_aa"&&launcher::readSetting(file,L"InSim",L"Enabled",L"0")==L"1"?std::function<void()>(setupRepairStartup):std::function<void()>{});
    setupMonitorLogs();
    auto activationError=setupEnvironment(L"XPFSR24_ACTIVATION_ERROR");
    if(!connected){
        const auto owner=xp_renderer::environment("XP_RENDERER_OWNER");
        if(owner=="native_aa"){
            setupPanel.configurationOnly("Native AA is active, so FSR is off for this session. To switch, choose FSR under Image processor on Rendering, then restart. Both mods keep your settings.",true);
            setupPanel.activity="NATIVE AA SESSION - FSR / NR / frame generation inactive";
        }
        else if(!activationError.empty())setupPanel.configurationOnly("Startup bypass: "+insim::utf8(activationError)+". Saved settings retained; correct the setup and restart X-Plane.",true);
        else setupPanel.configurationOnly(setupEnvironment(L"DISABLE_XPFSR24").empty()?"Saved settings recovered. The renderer did not start. Select Repair startup link, then close and reopen X-Plane using your usual shortcut.":"Native comparison was requested for this launch. Rendering is bypassed; your saved choices remain available for the next session.");
        return;
    }
    auto guard=setupEnvironment(L"XPFSR24_FG_GUARD");if(!guard.empty())setupPanel.notice=insim::utf8(guard);
    auto pending=setupEnvironment(L"XPFSR24_PENDING_GRAPHICS");if(!pending.empty()&&activationError.empty())setupPanel.notice=insim::utf8(pending);
}
static void setupPoll(){
    if(!setupVisible())return;
    auto now=GetTickCount64();if(now-setupLastPoll<500)return;setupLastPoll=now;
    try{
        if(!setupPanel.configured())setupBind();
        setupPollImport();
        setupPollRepair();
        setupPanel.refresh();app::Telemetry status{};if(shared&&app::readStatus(shared,status))setupPanel.observe(status,now);
        if(!setupRoot.empty()&&app::validSessionToken(setupToken)){
            const auto folder=setupRoot/L"logs/reports";auto file=folder/(setupToken+L".status.txt");bool previous=false;
            if(!std::filesystem::exists(file)){file=folder/L"last-status.txt";previous=true;}
            if(std::filesystem::is_regular_file(file)&&!std::filesystem::is_symlink(file)&&std::filesystem::file_size(file)<=4096){
                auto text=graphics::read(file);if(previous){const auto first=text.find('\n');text=first==std::string::npos?std::string{}:"Previous session: "+text.substr(first+1);}
                if(!text.empty())setupPanel.reportStatus=text.substr(0,240);
            }
        }
        if(setupPanel.rendererLinked()&&!setupRoot.empty()){
            std::ifstream in(setupRoot/L"logs/renderer-nr-status.txt");char data[4097]{};in.read(data,4096);std::istringstream text(std::string(data,size_t(in.gcount())));
            DWORD pid{};ULONGLONG ticks{};uint64_t records{};std::string token,message;
            if(text>>pid>>ticks>>records){text.ignore(1024,'\n');std::getline(text,token);std::getline(text,message);
                if(pid==GetCurrentProcessId()&&ticks<=now&&now-ticks<3000&&token==std::string(setupToken.begin(),setupToken.end()))setupPanel.nrStatus=message;else setupPanel.nrStatus="Waiting for current-session NR status";}
        }
    }catch(const std::exception&e){setupPanel.notice=e.what();}catch(...){setupPanel.notice="In-sim status could not be refreshed";}
}
// The visual state is local to the window. It never changes renderer settings or input geometry.
namespace setup_deck {
struct Ink {float r,g,b;};
constexpr Ink canvas{.066f,.079f,.093f}, deck{.088f,.104f,.121f}, field{.119f,.143f,.162f},
    border{.198f,.233f,.257f}, rule{.145f,.169f,.186f}, white{.927f,.945f,.948f},
    dim{.650f,.704f,.730f}, ice{.454f,.925f,.845f}, tint{.113f,.225f,.218f},
    amber{.925f,.749f,.465f};
static Ink mix(Ink a,Ink b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
static void ink(Ink c){glColor4f(c.r,c.g,c.b,1);}
static void solid(float x,float y,float w,float h,Ink color){
    if(w<=0||h<=0)return;
    ink(color);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y-h);glVertex2f(x,y-h);glEnd();
}
static void line(float x1,float y1,float x2,float y2,float stroke,Ink color){
    const float dx=x2-x1,dy=y2-y1,length=std::sqrt(dx*dx+dy*dy);
    if(!length)return;
    const float nx=-dy*stroke/(2*length),ny=dx*stroke/(2*length);
    ink(color);glBegin(GL_QUADS);glVertex2f(x1+nx,y1+ny);glVertex2f(x2+nx,y2+ny);
    glVertex2f(x2-nx,y2-ny);glVertex2f(x1-nx,y1-ny);glEnd();
}
// Unit arc vertices are computed once, not on each draw call.
struct Corners{
    float x[4][7],y[4][7];
    Corners(){for(int c=0;c<4;++c)for(int i=0;i<=6;++i){
        const float angle=float((-90+c*90+i*15)*3.141592653589793/180);
        x[c][i]=std::cos(angle);y[c][i]=std::sin(angle);
    }}
};
static void rounded(float x,float y,float w,float h,float radius,Ink color){
    if(w<=0||h<=0)return;
    static const Corners arc{};
    radius=std::min(radius,std::min(w,h)*.5f);
    ink(color);glBegin(GL_TRIANGLE_FAN);glVertex2f(x+w/2,y-h/2);
    for(int c=0;c<4;++c){
        const float cx=x+(c<2?w-radius:radius),cy=y-(c==0||c==3?radius:h-radius);
        for(int i=0;i<=6;++i)glVertex2f(cx+arc.x[c][i]*radius,cy-arc.y[c][i]*radius);
    }
    glVertex2f(x+w-radius,y);glEnd();
}
static float measure(const std::string& s){return XPLMMeasureString(xplmFont_Proportional,s.data(),int(s.size()));}
static void text(int x,int y,int width,const std::string &value,Ink color=white){
    if(width<=0||value.empty())return;
    std::string s=value;
    if(measure(s)>width){
        const std::string dots="...";
        int bytes=int(s.size()),lo=0,hi=bytes;
        const float available=width-measure(dots);
        if(available<=0)return;
        while(lo<hi){
            int mid=lo+(hi-lo+1)/2;
            if(XPLMMeasureString(xplmFont_Proportional,s.data(),mid)<=available)lo=mid;else hi=mid-1;
        }
        while(lo>0&&lo<bytes&&(static_cast<unsigned char>(s[lo])&0xc0)==0x80)--lo;
        s.resize(lo);s+=dots;
    }
    float rgb[]={color.r,color.g,color.b};
    XPLMDrawString(rgb,x,y,s.data(),nullptr,xplmFont_Proportional);
}
static std::vector<std::string> wrap(int width,const std::string& value){
    std::vector<std::string> lines;std::istringstream paragraphs(value);std::string paragraph;
    while(std::getline(paragraphs,paragraph)){
        std::istringstream input(paragraph);std::string word,line;
        while(input>>word){
            auto next=line.empty()?word:line+" "+word;
            if(!line.empty()&&measure(next)>width){lines.push_back(line);line=word;}else line=next;
        }
        lines.push_back(line);
    }
    return lines;
}
static void wrapped(int x,int y,int width,const std::string& value,int rows,Ink color,int skip=0){
    const auto lines=wrap(width,value);
    for(int i=0;i<rows&&i+skip<int(lines.size());++i)text(x,y-i*17,width,lines[i+skip],color);
}
static float ease(float t){t=std::clamp(t,0.f,1.f);return 1-(1-t)*(1-t)*(1-t);}
struct Sweep{
    float from{},to{};
    ULONGLONG start{};
    bool initialized{};
    float value(ULONGLONG now)const{
        return from+(to-from)*ease(float(now-start)/210.f);
    }
    float sample(float target,ULONGLONG now,bool motion){
        if(!initialized||!motion){initialized=true;from=to=target;start=now;return target;}
        if(target!=to){from=value(now);to=target;start=now;}
        return value(now);
    }
};
static Sweep nav;
static std::map<std::pair<int,int>,Sweep> switches;
static bool motion=true;
static ULONGLONG opened{},pageChanged{};
static int page=-1;
static void begin(){
    BOOL animated=TRUE;
    if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animated,0))motion=animated!=FALSE;
    opened=GetTickCount64();page=-1;nav={};switches.clear();
}
static void brand(int left,int top,float opening){
    // Three receding image planes are an original mark for the reconstruction controls.
    const int x=left+24,y=top-18;
    for(int i=2;i>=0;--i){
        const float d=i*(4+4*(1-opening));
        Ink c=mix(ice,border,i*.38f);
        line(x,y-12-d,x+14,y-3-d,2,c);line(x+14,y-3-d,x+28,y-12-d,2,c);
        line(x+28,y-12-d,x+14,y-21-d,2,c);line(x+14,y-21-d,x,y-12-d,2,c);
    }
    // A chamfered wordmark gives the panel a display face independent of the host UI font.
    const int gx=x+42,gy=top-21;
    auto bar=[&](float a,float b,float c,float d){line(gx+a,gy-b,gx+c,gy-d,3.8f,white);};
    bar(1,0,1,22);bar(0,1,17,1);bar(1,10,14,10); // F
    bar(25,1,40,1);bar(25,1,22,4);bar(22,3,22,10);bar(21,10,37,10);
    bar(36,10,39,13);bar(39,12,39,18);bar(39,18,36,21);bar(36,21,21,21); // S
    bar(47,0,47,22);bar(46,1,60,1);bar(60,1,63,4);bar(63,3,63,8);
    bar(63,8,60,11);bar(60,11,47,11);bar(53,12,64,22); // R
    solid(gx+79,top-18,1,30,border);
    text(gx+92,top-28,225,"Upscaling   /   3.1 + 4.1",white);
    text(gx+92,top-45,225,"Frame gen   /   3.1 + 4.0",dim);
}
static void draw(XPLMWindowID h){
    if(!XPLMGetWindowIsVisible(h))return;
    int left,top,right,bottom,mx,my;
    XPLMGetWindowGeometry(h,&left,&top,&right,&bottom);XPLMGetMouseLocationGlobal(&mx,&my);
    const int width=right-left,height=top-bottom;
    const auto now=GetTickCount64();
    const int pageKey=setupPanel.helpField>=0?1000+setupPanel.helpField:
        setupPanel.dropdownOpen?10000+int(setupPanel.dropdown.kind)*100+setupPanel.dropdown.field:setupPanel.tab;
    if(page!=pageKey){page=pageKey;pageChanged=now;}
    const float entering=motion?ease(float(now-pageChanged)/200.f):1.f;
    const float opening=motion&&opened?ease(float(now-opened)/430.f):1.f;
    XPLMSetGraphicsState(0,0,0,0,1,0,0);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    solid(left,top,width,height,canvas);solid(left,top,width,148,deck);
    solid(left+24,top-55,width-48,1,rule);
    solid(left+24,top-55,54*opening,1,ice);
    brand(left,top,opening);
    const bool running=setupPanel.activity.rfind("FSR ACTIVE",0)==0;
    const bool bypass=setupPanel.activity.find("BYPASSED")!=std::string::npos;
    const Ink lamp=running?ice:bypass?amber:dim;
    rounded(left+25,top-67,7,7,3.5f,lamp);
    solid(left+24,top-146,width-48,1,border);
    const float activeTab=nav.sample(float(setupPanel.tab),now,motion);
    const int tabWidth=(width-48)/4;
    solid(left+24+activeTab*tabWidth,top-145,tabWidth,2,ice);

    const int foot=top-height+insim::footerHeight;
    solid(left,foot,width,insim::footerHeight,deck);solid(left+24,foot,width-48,1,border);
    const auto items=setupPanel.view(width,height);
    for(const auto& item:items){
        const auto &r=item.rect;const int x=left+r.x,y=top-r.y;
        const bool hover=item.button&&r.contains(mx-left,top-my);
        const bool content=r.y>=insim::contentTop&&r.y<height-insim::footerHeight;
        auto shade=[&](Ink c){return content?mix(canvas,c,.52f+.48f*entering):c;};
        const auto kind=item.action.kind;
        const int baseline=y-(r.h/2+5);
        if(item.widget==insim::Widget::Rule){solid(x,y,r.w,1,rule);continue;}
        if(item.widget==insim::Widget::Section){
            const float labelWidth=std::min(float(r.w-16),measure(item.text)+4);
            text(x+3,y-21,r.w-6,item.text,shade(ice));
            const float timingWidth=item.detail.empty()?0:measure(item.detail)+15;
            solid(x+labelWidth+12,y-16,r.w-labelWidth-12-timingWidth,1,rule);
            if(timingWidth)text(int(x+r.w-timingWidth+15),y-21,int(timingWidth),item.detail,shade(item.detail=="Live"?ice:dim));
            continue;
        }
        if(!item.button){
            const Ink c=item.widget==insim::Widget::Status?lamp:item.widget==insim::Widget::Reading?ice:
                item.widget==insim::Widget::Muted?dim:white;
            if(r.h>=34)wrapped(x,y-14,r.w,item.text,std::max(1,r.h/17),shade(c));
            else text(x,y-14,r.w,item.text,shade(c));
            continue;
        }
        if(kind==insim::Kind::Tab){
            if(hover&&!item.selected)rounded(x+1,y-3,r.w-2,r.h-7,4,field);
            const std::string index="0"+std::to_string(item.action.field+1);
            text(x+7,baseline,23,index,item.selected?ice:dim);
            text(x+32,baseline,r.w-36,item.text,item.selected?white:dim);
            continue;
        }
        if(item.widget==insim::Widget::Toggle){
            const float v=switches[{int(kind),item.action.field}].sample(item.selected?1.f:0.f,now,motion);
            const int tx=x+r.w-45,ty=y-(r.h-20)/2;
            rounded(tx,ty,43,20,10,shade(mix(hover?dim:border,ice,v)));
            rounded(tx+1,ty-1,41,18,9,shade(mix(field,tint,v)));
            rounded(tx+4+23*v,ty-4,12,12,6,shade(mix(dim,ice,v)));
            text(x+3,baseline,r.w-53,item.text,shade(item.selected?ice:dim));
            continue;
        }
        if(item.widget==insim::Widget::Slider){
            const int value=setupDragField==item.action.field?setupDragPreview:item.value;
            const float span=std::max(1,r.w-16),f=float(value-item.minimum)/std::max(1,item.maximum-item.minimum);
            const float cy=y-r.h*.5f,k=x+8+span*f;
            rounded(x+8,cy+2,span,4,2,shade(border));rounded(x+8,cy+2,span*f,4,2,shade(ice));
            rounded(k-5,cy+6,10,12,4,shade(hover?white:ice));continue;
        }
        if(kind==insim::Kind::Help&&item.action.field>=0){
            rounded(x+(r.w-20)/2,y-(r.h-20)/2,20,20,10,shade(hover?ice:border));
            rounded(x+(r.w-18)/2,y-(r.h-18)/2,18,18,9,shade(hover?tint:canvas));
            text(int(x+(r.w-measure("?"))/2),baseline,14,"?",shade(hover?ice:dim));continue;
        }
        const bool utility=kind==insim::Kind::Changelog||kind==insim::Kind::Updates||kind==insim::Kind::Close;
        if(utility){
            if(hover)rounded(x,y,r.w,r.h,4,field);
            text(int(x+(r.w-measure(item.text))/2),baseline,r.w,item.text,hover?white:dim);continue;
        }
        const auto fill=shade(item.selected?tint:hover?mix(field,border,.4f):field);
        rounded(x,y,r.w,r.h,5,shade(item.selected||hover?mix(border,ice,.60f):border));
        rounded(x+1,y-1,r.w-2,r.h-2,4,fill);
        const bool choice=item.widget==insim::Widget::Choice;
        int pad=12;
        if(item.selected){solid(x+6,y-10,2,r.h-20,shade(ice));pad=15;}
        text(x+pad,baseline,r.w-pad-(choice?34:12),item.text,shade(item.selected?ice:white));
        if(choice){line(x+r.w-22,baseline+7,x+r.w-18,baseline+3,1.5f,shade(dim));line(x+r.w-18,baseline+3,x+r.w-14,baseline+7,1.5f,shade(dim));}
    }
    if(setupPanel.helpField>=0){
        const int rows=std::max(1,(height-insim::contentTop-insim::footerHeight-62)/17);
        const int maxSkip=std::max(0,int(wrap(width-48,setupPanel.help()).size())-rows);
        setupPanel.helpScroll=std::clamp(setupPanel.helpScroll,0,maxSkip);
        wrapped(left+24,top-insim::contentTop-50,width-48,setupPanel.help(),rows,white,setupPanel.helpScroll);
        if(maxSkip)text(left+24,foot+7,width-48,"Scroll to read more.",dim);
    }else{
        const int maximum=setupPanel.dropdownOpen?setupPanel.maxDropdownScroll(height):setupPanel.maxScroll(height);
        if(maximum>0){
            const int track=std::max(1,height-insim::contentTop-insim::footerHeight-5);
            const int thumb=std::max(19,track*track/(track+maximum));
            const int pos=(track-thumb)*(setupPanel.dropdownOpen?setupPanel.dropdownScroll:setupPanel.scroll)/maximum;
            rounded(right-11,top-insim::contentTop,3,track,1,rule);
            rounded(right-11,top-insim::contentTop-pos,3,thumb,1,mix(border,ice,.55f));
        }
    }
}
}
static void setupDraw(XPLMWindowID window,void*) noexcept{
    try{setup_deck::draw(window);}catch(...){/* keep exceptions inside the XPLM callback */ }
}
static int setupMouse(XPLMWindowID h,int x,int y,XPLMMouseStatus mouse,void*) noexcept{try{
    int left,top,right,bottom;XPLMGetWindowGeometry(h,&left,&top,&right,&bottom);if(setupDragField<0&&(x<left||x>=right||y<bottom||y>top))return 0;
    auto items=setupPanel.view(right-left,top-bottom);
    for(const auto &item:items){
        if(item.widget==insim::Widget::Slider&&((mouse==xplm_MouseDown&&item.rect.contains(x-left,top-y))||setupDragField==item.action.field)){
            setupDragField=item.action.field;auto now=GetTickCount64();
            float fraction=std::clamp(float(x-left-item.rect.x-8)/std::max(1,item.rect.w-16),0.f,1.f);setupDragPreview=int(std::lround(item.minimum+fraction*(item.maximum-item.minimum)));
            if(mouse==xplm_MouseDown||mouse==xplm_MouseUp||now-setupLastDrag>=100){setupPanel.slider(setupDragField,setupDragPreview);setupLastDrag=now;}
            if(mouse==xplm_MouseUp)setupDragField=-1;return 1;
        }
        if(mouse==xplm_MouseDown&&item.button&&item.rect.contains(x-left,top-y)){if(item.action.kind==insim::Kind::Close)setupHide();else setupPanel.act(item.action);return 1;}
    }
    if(mouse==xplm_MouseUp)setupDragField=-1;
    return 1;
}catch(const std::exception&e){setupDragField=-1;setupPanel.notice=e.what();return 1;}catch(...){setupDragField=-1;return 1;}}
static void setupKey(XPLMWindowID,char key,XPLMKeyFlags flags,char,void*,int losing) noexcept{if(!losing&&(flags&xplm_DownFlag)&&key==27){if(setupPanel.dropdownOpen)setupPanel.dropdownOpen=false;else if(setupPanel.helpField>=0)setupPanel.helpField=-1;else setupHide();}}
static XPLMCursorStatus setupCursor(XPLMWindowID,int,int,void*){return xplm_CursorDefault;}
static int setupRightMouse(XPLMWindowID h,int x,int y,XPLMMouseStatus,void*) noexcept{int l,t,r,b;XPLMGetWindowGeometry(h,&l,&t,&r,&b);return x>=l&&x<r&&y>=b&&y<=t;}
static int setupWheel(XPLMWindowID h,int x,int y,int wheel,int clicks,void*) noexcept{int left,top,right,bottom;XPLMGetWindowGeometry(h,&left,&top,&right,&bottom);if(x<left||x>=right||y<bottom||y>top)return 0;if(wheel==0){if(setupPanel.helpField>=0){const int rows=std::max(1,(top-bottom-insim::contentTop-insim::footerHeight-62)/17),maximum=std::max(0,int(setup_deck::wrap(right-left-48,setupPanel.help()).size())-rows);setupPanel.helpScroll=std::clamp(setupPanel.helpScroll-clicks*3,0,maximum);}else setupPanel.wheel(-clicks*46,top-bottom);}return 1;}
struct SetupMonitor {int mouseX{},mouseY{},left{},top{},right{},bottom{};bool found{};};
static void setupMonitor(int,int left,int top,int right,int bottom,void*ref){auto &m=*static_cast<SetupMonitor*>(ref);if(!m.found||(m.mouseX>=left&&m.mouseX<right&&m.mouseY>=bottom&&m.mouseY<top)){m.left=left;m.top=top;m.right=right;m.bottom=bottom;m.found=true;}}
static void setupShow() noexcept{try{
    if(!setupVisible())setup_deck::begin();
    if(!setupWindow){SetupMonitor m;XPLMGetMouseLocationGlobal(&m.mouseX,&m.mouseY);XPLMGetAllMonitorBoundsGlobal(setupMonitor,&m);if(!m.found)XPLMGetScreenBoundsGlobal(&m.left,&m.top,&m.right,&m.bottom);
        int width=std::min(880,std::max(1,m.right-m.left-40)),height=std::min(740,std::max(1,m.top-m.bottom-120));
        XPLMCreateWindow_t p{};p.structSize=sizeof(p);p.left=m.left+(m.right-m.left-width)/2;p.right=p.left+width;p.top=m.top-76;p.bottom=p.top-height;p.visible=1;p.drawWindowFunc=setupDraw;p.handleMouseClickFunc=setupMouse;p.handleRightClickFunc=setupRightMouse;p.handleKeyFunc=setupKey;p.handleCursorFunc=setupCursor;p.handleMouseWheelFunc=setupWheel;p.decorateAsFloatingWindow=xplm_WindowDecorationRoundRectangle;p.layer=xplm_WindowLayerFloatingWindows;
        setupWindow=XPLMCreateWindowEx(&p);if(!setupWindow)throw std::runtime_error("Cannot create the in-sim setup panel");XPLMSetWindowTitle(setupWindow,product::panelTitle);XPLMSetWindowResizingLimits(setupWindow,std::min(640,width),std::min(480,height),1280,1100);
    }else XPLMSetWindowIsVisible(setupWindow,1);
    setupPresence.setVisible(true);setupLastPoll=0;setupPoll();XPLMBringWindowToFront(setupWindow);
}catch(const std::exception&e){std::string text="XPFSR24: setup panel: ";text+=e.what();text+='\n';XPLMDebugString(text.c_str());}catch(...){}}
static void setupStop(){setupHide();if(setupWindow)XPLMDestroyWindow(setupWindow);setupWindow=nullptr;setupNR.close();setupPresence.close();if(setupGraphicsJob)CloseHandle(setupGraphicsJob);setupGraphicsJob=nullptr;if(setupImportJob)CloseHandle(setupImportJob);setupImportJob=nullptr;if(setupLogJob)CloseHandle(setupLogJob);setupLogJob=nullptr;if(setupRepairJob)CloseHandle(setupRepairJob);setupRepairJob=nullptr;setupLogToken.clear();setupConfigurationSession.close();}
