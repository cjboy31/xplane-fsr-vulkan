// SPDX-License-Identifier: MIT
// Original XPLM UI. It never requests camera/joystick control or global keys.
#include "in_sim_panel.hpp"
#include "panel_presence.hpp"
#include "nr_model_import.hpp"
#include "session_logs.hpp"
#include "in_sim_binding.hpp"
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <GL/gl.h>
#include <cmath>
static XPLMWindowID setupWindow{};
static insim::Panel setupPanel;
static xpfsr::nr::ControlChannel setupNR;
static app::PanelPresenceChannel setupPresence;
static ULONGLONG setupLastPoll{};
static std::filesystem::path setupRoot;
static std::wstring setupToken;
static HANDLE setupGraphicsJob{};
static HANDLE setupImportJob{};
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
    auto registered=RegGetValueW(HKEY_CURRENT_USER,L"SOFTWARE\\XPFSR24\\InSim",L"Manifest",RRF_RT_REG_SZ,nullptr,manifest,&manifestBytes)==ERROR_SUCCESS?std::filesystem::path(manifest):std::filesystem::path{};
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    auto binding=insim::resolveSettings(executable,setupEnvironment(L"XPFSR24_SETTINGS"),setupEnvironment(L"XPFSR24_RUNTIME"),registered);
    if(binding.file.empty())return;
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
        auto token=setupConfigurationSession.open(setupRoot);
        setupToken=std::wstring(token.begin(),token.end());SetEnvironmentVariableW(L"XPFSR24_CONFIG_SESSION",setupToken.c_str());
    }
    bool canNR=connected&&name==L"Local\\XPFSR24-NR-v4-"+setupToken&&setupNR.open(name.c_str())&&setupNR.read(live);
    auto fg=setupEnvironment(L"XPFSR24_FG");
    setupPanel.bind(file,canNR?&setupNR:nullptr,canNR,!fg.empty(),[](const launcher::Preferences&p){
        if(shared)InterlockedExchange(&shared->enabled,p.enabled?1:0);
        auto path=setupEnvironment(L"XPFSR24_CONTROL");
        if(!path.empty()&&std::filesystem::weakly_canonical(path)==std::filesystem::weakly_canonical(setupRoot/L"logs/renderer-control.txt")){
            std::ofstream out{std::filesystem::path(path)};out<<(p.enabled?'1':'0');if(!out)throw std::runtime_error("Saved, but the live processing control could not be updated");}
    });
    setupPanel.graphicsSupport(setupQueueGraphics);
    setupPanel.modelSupport(setupImportModel);
    if(!connected){setupPanel.configurationOnly(setupEnvironment(L"DISABLE_XPFSR24").empty()?"Saved settings recovered. The Vulkan renderer was not loaded at startup. Restart X-Plane after enabling in-sim integration; renderer activation cannot be added to an existing graphics device.":"Native comparison was requested for this launch. Rendering is bypassed; your saved choices remain available for the next session.");return;}
    auto guard=setupEnvironment(L"XPFSR24_FG_GUARD");if(!guard.empty())setupPanel.notice=insim::utf8(guard);
    auto activationError=setupEnvironment(L"XPFSR24_ACTIVATION_ERROR");if(!activationError.empty())setupPanel.notice="Renderer bypassed: "+insim::utf8(activationError)+". Correct the saved profile here for next launch.";
    auto pending=setupEnvironment(L"XPFSR24_PENDING_GRAPHICS");if(!pending.empty()&&activationError.empty())setupPanel.notice=insim::utf8(pending);
}
static void setupPoll(){
    if(!setupVisible())return;
    auto now=GetTickCount64();if(now-setupLastPoll<500)return;setupLastPoll=now;
    try{
        if(!setupPanel.configured())setupBind();
        setupPollImport();
        setupPanel.refresh();app::Telemetry status{};if(shared&&app::readStatus(shared,status))setupPanel.observe(status,now);
        if(setupPanel.rendererLinked()&&!setupRoot.empty()){
            std::ifstream in(setupRoot/L"logs/renderer-nr-status.txt");char data[4097]{};in.read(data,4096);std::istringstream text(std::string(data,size_t(in.gcount())));
            DWORD pid{};ULONGLONG ticks{};uint64_t records{};std::string token,message;
            if(text>>pid>>ticks>>records){text.ignore(1024,'\n');std::getline(text,token);std::getline(text,message);
                if(pid==GetCurrentProcessId()&&ticks<=now&&now-ticks<3000&&token==std::string(setupToken.begin(),setupToken.end()))setupPanel.nrStatus=message;else setupPanel.nrStatus="Waiting for current-session NR status";}
        }
    }catch(const std::exception&e){setupPanel.notice=e.what();}catch(...){setupPanel.notice="In-sim status could not be refreshed";}
}
static void setupRound(float x,float y,float w,float h,float radius){
    radius=std::min(radius,std::min(w,h)/2);glBegin(GL_TRIANGLE_FAN);glVertex2f(x+w/2,y-h/2);
    for(int corner=0;corner<4;++corner){float cx=x+(corner<2?w-radius:radius),cy=y-(corner==0||corner==3?radius:h-radius);for(int i=0;i<=8;++i){float angle=float((-90+corner*90+i*90.0/8)*3.141592653589793/180);glVertex2f(cx+std::cos(angle)*radius,cy-std::sin(angle)*radius);}}
    glVertex2f(x+w-radius,y);glEnd();
}
static void setupBox(float left,float top,const insim::Rect&r,bool selected){
    if(selected)glColor4f(.045f,.28f,.34f,1);else glColor4f(.075f,.105f,.15f,1);
    setupRound(left+r.x,top-r.y,float(r.w),float(r.h),6);
}
static void setupText(int x,int y,int width,const std::string &value,bool accent=false){
    std::string clipped=value;while(!clipped.empty()&&XPLMMeasureString(xplmFont_Proportional,clipped.data(),int(clipped.size()))>width)clipped.pop_back();
    float color[3]={accent?.27f:.9f,accent?.87f:.94f,accent?.93f:.98f};XPLMDrawString(color,x,y,clipped.data(),nullptr,xplmFont_Proportional);
}
static void setupWrapped(int x,int y,int width,const std::string &value,int lines){
    std::istringstream input(value);std::string word,line;int row=0;
    while(input>>word){auto next=line.empty()?word:line+" "+word;if(!line.empty()&&XPLMMeasureString(xplmFont_Proportional,next.data(),int(next.size()))>width){setupText(x,y-row*17,width,line);line=word;if(++row>=lines)return;}else line=next;}
    if(row<lines)setupText(x,y-row*17,width,line);
}
static void setupDraw(XPLMWindowID h,void*) noexcept{try{
    if(!XPLMGetWindowIsVisible(h))return;
    int left,top,right,bottom;XPLMGetWindowGeometry(h,&left,&top,&right,&bottom);int width=right-left,height=top-bottom;
    XPLMSetGraphicsState(0,0,0,0,1,0,0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(.018f,.026f,.043f,1);
    glBegin(GL_QUADS);glVertex2i(left,top);glVertex2i(right,top);glVertex2i(right,bottom);glVertex2i(left,bottom);glEnd();
    for(const auto &item:setupPanel.view(width,height)){
        const auto &r=item.rect;const float x=float(left+r.x),y=float(top-r.y);
        if(r.y==height-102){setupWrapped(int(x),int(y)-14,r.w,item.text,2);continue;}
        if(item.widget==insim::Widget::Label&&r.h>20){setupWrapped(int(x),int(y)-14,r.w,item.text,3);continue;}
        if(item.widget==insim::Widget::Toggle){
            const float tx=x+r.w-52,ty=y-(r.h-24)/2;
            glColor4f(item.selected?.025f:.12f,item.selected?.52f:.16f,item.selected?.59f:.22f,1);setupRound(tx,ty,48,24,12);
            glColor4f(.88f,.96f,1,1);setupRound(tx+(item.selected?26:4),ty-4,16,16,8);
            setupText(int(x)+8,int(y)-22,r.w-68,item.text,item.selected);
        }else if(item.widget==insim::Widget::Slider){
            int value=setupDragField==item.action.field?setupDragPreview:item.value;
            float span=float(std::max(1,r.w-16)),fraction=float(value-item.minimum)/std::max(1,item.maximum-item.minimum),cy=y-r.h/2;
            glColor4f(.10f,.15f,.22f,1);setupRound(x+8,cy+3,span,6,3);
            glColor4f(.07f,.65f,.76f,1);if(fraction>0)setupRound(x+8,cy+3,span*fraction,6,3);
            glColor4f(.73f,.95f,1,1);setupRound(x+8+span*fraction-6,cy+6,12,12,6);
        }else{
            if(item.button)setupBox(float(left),float(top),r,item.selected);
            setupText(int(x)+(item.button?12:0),int(y)-(item.button?21:14),r.w-(item.button?24:0),item.text,item.selected||r.y==20);
            if(item.widget==insim::Widget::Choice)setupText(int(x)+r.w-25,int(y)-20,18,"v",true);
        }
    }
    if(setupPanel.helpField>=0)setupWrapped(left+24,top-insim::contentTop-62,width-48,setupPanel.help(),std::max(1,(height-insim::contentTop-198)/17));
    else setupWrapped(left+24,bottom+69,width-48,setupPanel.dropdownOpen?"Choose an option. Scroll to reveal more choices; Back returns without changing the selection.":setupPanel.help(),3);
    if(setupPanel.helpField<0&&(setupPanel.dropdownOpen?setupPanel.maxDropdownScroll(height):setupPanel.maxScroll(height))>0){
        const int track=std::max(1,height-insim::contentTop-insim::footerHeight),maximum=setupPanel.dropdownOpen?setupPanel.maxDropdownScroll(height):setupPanel.maxScroll(height),thumb=std::max(24,track*track/(track+maximum));
        glColor4f(.12f,.65f,.75f,1);int y=insim::contentTop+(track-thumb)*(setupPanel.dropdownOpen?setupPanel.dropdownScroll:setupPanel.scroll)/std::max(1,maximum);
        glBegin(GL_QUADS);glVertex2i(right-13,top-y);glVertex2i(right-9,top-y);glVertex2i(right-9,top-y-thumb);glVertex2i(right-13,top-y-thumb);glEnd();
    }
}catch(...){/* No C++ exceptions cross the XPLM callback boundary. */}}
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
static int setupWheel(XPLMWindowID h,int x,int y,int wheel,int clicks,void*) noexcept{int left,top,right,bottom;XPLMGetWindowGeometry(h,&left,&top,&right,&bottom);if(x<left||x>=right||y<bottom||y>top)return 0;if(wheel==0)setupPanel.wheel(-clicks*46,top-bottom);return 1;}
struct SetupMonitor {int mouseX{},mouseY{},left{},top{},right{},bottom{};bool found{};};
static void setupMonitor(int,int left,int top,int right,int bottom,void*ref){auto &m=*static_cast<SetupMonitor*>(ref);if(!m.found||(m.mouseX>=left&&m.mouseX<right&&m.mouseY>=bottom&&m.mouseY<top)){m.left=left;m.top=top;m.right=right;m.bottom=bottom;m.found=true;}}
static void setupShow() noexcept{try{
    if(!setupWindow){SetupMonitor m;XPLMGetMouseLocationGlobal(&m.mouseX,&m.mouseY);XPLMGetAllMonitorBoundsGlobal(setupMonitor,&m);if(!m.found)XPLMGetScreenBoundsGlobal(&m.left,&m.top,&m.right,&m.bottom);
        int width=std::min(880,std::max(1,m.right-m.left-40)),height=std::min(740,std::max(1,m.top-m.bottom-120));
        XPLMCreateWindow_t p{};p.structSize=sizeof(p);p.left=m.left+(m.right-m.left-width)/2;p.right=p.left+width;p.top=m.top-76;p.bottom=p.top-height;p.visible=1;p.drawWindowFunc=setupDraw;p.handleMouseClickFunc=setupMouse;p.handleRightClickFunc=setupRightMouse;p.handleKeyFunc=setupKey;p.handleCursorFunc=setupCursor;p.handleMouseWheelFunc=setupWheel;p.decorateAsFloatingWindow=xplm_WindowDecorationRoundRectangle;p.layer=xplm_WindowLayerFloatingWindows;
        setupWindow=XPLMCreateWindowEx(&p);if(!setupWindow)throw std::runtime_error("Cannot create the in-sim setup panel");XPLMSetWindowTitle(setupWindow,"FSR 3/4 + Frame Gen - In-sim setup");XPLMSetWindowResizingLimits(setupWindow,std::min(640,width),std::min(480,height),1280,1100);
    }else XPLMSetWindowIsVisible(setupWindow,1);
    setupPresence.setVisible(true);setupLastPoll=0;setupPoll();XPLMBringWindowToFront(setupWindow);
}catch(const std::exception&e){std::string text="XPFSR24: setup panel: ";text+=e.what();text+='\n';XPLMDebugString(text.c_str());}catch(...){}}
static void setupStop(){setupHide();if(setupWindow)XPLMDestroyWindow(setupWindow);setupWindow=nullptr;setupNR.close();setupPresence.close();if(setupGraphicsJob)CloseHandle(setupGraphicsJob);setupGraphicsJob=nullptr;if(setupImportJob)CloseHandle(setupImportJob);setupImportJob=nullptr;setupConfigurationSession.close();}
