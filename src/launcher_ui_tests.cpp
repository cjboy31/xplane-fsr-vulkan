// SPDX-License-Identifier: MIT
// Exercise the real main window, without simulator launches or network access.
#define wWinMain unusedLauncherMain
#include "launcher.cpp"
#undef wWinMain
#include <iostream>
#include <map>

static std::map<HWND, unsigned> paints, moves;
static LRESULT CALLBACK countPaint(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_PAINT) ++paints[h];
    if (m == WM_WINDOWPOSCHANGED) ++moves[h];
    return DefSubclassProc(h,m,w,l);
}
static void pump() {
    MSG m;
    while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&m);DispatchMessageW(&m);
    }
    GdiFlush();
}
static RECT relativeRect(HWND h, HWND parent) {
    RECT r;GetWindowRect(h,&r);MapWindowPoints(HWND_DESKTOP,parent,reinterpret_cast<POINT*>(&r),2);
    return r;
}
static bool contains(RECT outer, RECT inner) {
    return inner.left>=outer.left && inner.top>=outer.top && inner.right<=outer.right && inner.bottom<=outer.bottom;
}
int wmain(int argc,wchar_t**argv) {
    try {
        ensure(argc==2,"Fresh test directory required");
        appDirectory=argv[1];ensure(!fs::exists(appDirectory),"Fixture must be new");fs::create_directories(appDirectory);
        theme::enableDpi();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        launcherWindowClass=L"XPFSRMainLayoutRegression";
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=launcherWindowClass.c_str();RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_COMPOSITED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"FSR layout regression",
            WS_POPUP|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1260,800,nullptr,nullptr,cls.hInstance,nullptr);
        ensure(window&&controlsReady,"Main controls did not initialize");
        for(UINT id:{1u,2u,3u,4u,5u})KillTimer(window,id);
        // The fixture is visible without activation so native child paint messages are real.
        ShowWindow(window,SW_SHOWNOACTIVATE);
        EnumChildWindows(window,[](HWND h,LPARAM)->BOOL{SetWindowSubclass(h,countPaint,91,0);return TRUE;},0);
        for(UINT dpi:{96u,120u,144u,192u}) {
            theme::dpi=dpi;theme::makeFonts();
            for(RECT work: {RECT{0,0,1024,728},RECT{-1280,100,0,800},RECT{2000,0,2800,568}}){
                auto size=modalSizeForWorkArea(1100,760,work),minimum=modalSizeForWorkArea(720,350,work);
                ensure(size.cx<=work.right-work.left&&size.cy<=work.bottom-work.top&&minimum.cx<=size.cx&&minimum.cy<=size.cy,"Modal size/minimum exceeds a small or secondary monitor");
            }
            EnumChildWindows(window,[](HWND h,LPARAM)->BOOL{int n=int(INT_PTR(GetPropW(h,L"XPFSRFont")));SendMessageW(h,WM_SETFONT,WPARAM(theme::fonts[std::clamp(n,0,5)]),FALSE);return TRUE;},0);
            for(int width:{840,1260}) {
                SetWindowPos(window,nullptr,0,0,theme::px(width),theme::px(740),SWP_NOZORDER|SWP_NOACTIVATE);
                designLayout(window);pump();
                auto footer=relativeRect(designControl(20),window);
                for(int next:{0,170,410,170,99999,0,99999}) {
                    paints.clear();designScrollTo(window,next);pump();
                    ensure(contains(designCards[4],relativeRect(statusBox,designCanvas)),"Live details escaped the status card");
                    for(int id:{34,36,37,38,31,32,540})
                        ensure(contains(designCards[6],relativeRect(designControl(id),designCanvas)),"Diagnostics controls overlap a card boundary");
                    auto actual=relativeRect(designControl(20),window);
                    ensure(EqualRect(&footer,&actual),"Scrolling moved the fixed launch footer");
                    RECT viewport;GetClientRect(designCanvas,&viewport);
                    for(HWND h:{statusBox,designHeads[6],designControl(32),designControl(540)}) {
                        RECT clipped,child=relativeRect(h,designCanvas);
                        if(IntersectRect(&clipped,&child,&viewport)&&next!=0)
                            ensure(paints[h]>0,"Scrolling left a visible status/diagnostic child without a fresh paint");
                    }
                }
                // Repeated overscroll must not move or invalidate the view again.
                for(int edge:{0,99999}) {
                    designScrollTo(window,edge);pump();paints.clear();moves.clear();
                    int position=designScroll;
                    for(int repeat=0;repeat<100;++repeat) {
                        designWheel(window,edge?-WHEEL_DELTA:WHEEL_DELTA);
                        SendMessageW(window,WM_VSCROLL,edge?SB_BOTTOM:SB_TOP,0);
                    }
                    pump();ensure(designScroll==position&&moves.empty()&&paints.empty(),"Overscrolling repainted or moved the window tree");
                }
                designScrollTo(window,0);designWheelRemainder=0;
                for(int i=0;i<3;++i)designWheel(window,-30);
                ensure(designScroll==0,"Partial wheel deltas caused premature scrolling");
                designWheel(window,-30);ensure(designScroll==72,"High-resolution wheel deltas were lost");
                pump();
                std::cout<<"PASS: scroll/clipping/diagnostics/footer/limits at "<<dpi<<" DPI, "<<width<<" logical px\n";
            }
        }
        // Main controls were reparented; runtime enabling must still find them.
        ensure(SendMessageW(fgBox,CB_GETCOUNT,0,0)==(app::frameGenerationAvailable?5:1),"Compatibility FG choices or Normal suspension gating are incorrect");
        if(app::frameGenerationAvailable){
            SendMessageW(fgBox,CB_SETCURSEL,3,0);saveChoices();
            ensure(launcher::load(appDirectory/L"settings.ini").frameGeneration==3,"Compatibility FG choice was not saved");
            SendMessageW(fgBox,CB_SETCURSEL,0,0);saveChoices();
        }else ensure(!IsWindowEnabled(fgBox),"Normal release enabled experimental FG choices");
        child=CreateEventW(nullptr,TRUE,FALSE,nullptr);refresh();
        ensure(!IsWindowEnabled(modeBox)&&!IsWindowEnabled(presetBox)&&IsWindowEnabled(neuralBox)==app::neuralRenderingAvailable,
               "Runtime control gating lost reparented controls");
        SetEvent(child);CloseHandle(child);child=nullptr;refresh();ensure(IsWindowEnabled(modeBox)&&IsWindowEnabled(presetBox),"Controls did not unlock");
        if(app::neuralRenderingAvailable) {
            showNRSetup();KillTimer(nrWindow,1);
            RECT modalBounds{};GetWindowRect(nrWindow,&modalBounds);
            MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            ensure(modalBounds.left>=monitor.rcWork.left&&modalBounds.top>=monitor.rcWork.top&&modalBounds.right<=monitor.rcWork.right&&modalBounds.bottom<=monitor.rcWork.bottom,"NR settings escaped the owner's monitor");
            EnumChildWindows(nrWindow,[](HWND h,LPARAM)->BOOL{SetWindowSubclass(h,countPaint,91,0);return TRUE;},0);
            ensure(SendMessageW(nrFields[xpfsr::nr::Scope],CB_GETCOUNT,0,0)==2,"Both NR scopes must be available");
            SendMessageW(nrFields[xpfsr::nr::Scope],CB_SETCURSEL,1,0);nrChangeSetting(170+xpfsr::nr::Scope);
            ensure(launcher::load(appDirectory/L"settings.ini").nr[xpfsr::nr::Scope]==1,"Full-scene scope did not save");
            ensure(SendMessageW(nrFields[xpfsr::nr::Order],CB_GETCOUNT,0,0)==2,"Both NR processing orders must be available");
            ensure(SendMessageW(nrFields[xpfsr::nr::Order],CB_GETCURSEL,0,0)==0,"Before upscaling must be the initial selection");
            xpfsr::nr::ControlChannel reader;auto name=L"Local\\NR-Order-UI-"+std::to_wstring(GetCurrentProcessId());
            ensure(nrChannel.create(name.c_str(),preferences.nr)&&reader.open(name.c_str()),"NR live test channel failed");
            child=CreateEventW(nullptr,TRUE,FALSE,nullptr);launchedNative=false;launchedMode=2;launchedFG=0;
            const auto originalMode=preferences.mode,originalFG=preferences.frameGeneration;
            for(int order:{1,0,1}){
                SendMessageW(nrFields[xpfsr::nr::Order],CB_SETCURSEL,order,0);
                SendMessageW(nrBody,WM_COMMAND,MAKEWPARAM(170+xpfsr::nr::Order,CBN_SELCHANGE),LPARAM(nrFields[xpfsr::nr::Order]));
                xpfsr::nr::Controls snapshot;ensure(reader.read(snapshot)&&snapshot[xpfsr::nr::Order]==order,"NR order was not sent live");
                ensure(launcher::load(appDirectory/L"settings.ini").nr[xpfsr::nr::Order]==order,"NR processing order did not save");
                ensure(preferences.mode==originalMode&&preferences.frameGeneration==originalFG,"NR order changed FSR/FG");
            }
            SetEvent(child);CloseHandle(child);child=nullptr;nrChannel.close();reader.close();
            for(int edge:{0,99999}) {
                nrScrollTo(edge);pump();paints.clear();moves.clear();int position=nrScroll;
                for(int repeat=0;repeat<100;++repeat) {
                    SendMessageW(nrWindow,WM_MOUSEWHEEL,MAKEWPARAM(0,edge?-WHEEL_DELTA:WHEEL_DELTA),0);
                    SendMessageW(nrWindow,WM_VSCROLL,edge?SB_BOTTOM:SB_TOP,0);
                }
                pump();ensure(nrScroll==position&&moves.empty()&&paints.empty(),"NR modal overscroll repainted or moved the view");
            }
            SendMessageW(nrWindow,WM_COMMAND,IDCANCEL,0);ensure(!nrWindow&&IsWindowEnabled(window),"Escape did not close NR settings");
            showNRSetup();KillTimer(nrWindow,1);
            ensure(SendMessageW(nrFields[xpfsr::nr::Order],CB_GETCURSEL,0,0)==1,"Reopening NR settings lost the order selection");
            SendMessageW(nrWindow,WM_COMMAND,IDCANCEL,0);
            std::cout<<"PASS: live NR processing order, saved/reopened selection, scopes, modal scroll limits and Escape\n";
        }
        fs::create_directories(appDirectory/L"logs");
        activeSessionToken=session::prepare(appDirectory/L"logs","live-log-fixture");
        child=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        showRendererLog();KillTimer(rendererLogWindow,1);
        RECT logBounds{};GetWindowRect(rendererLogWindow,&logBounds);MONITORINFO logMonitor{sizeof(logMonitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&logMonitor);
        ensure(contains(logMonitor.rcWork,logBounds),"Live log viewer opened outside the owner's monitor");
        MINMAXINFO logLimits{};SendMessageW(rendererLogWindow,WM_GETMINMAXINFO,0,LPARAM(&logLimits));
        ensure(logLimits.ptMinTrackSize.x<=logMonitor.rcWork.right-logMonitor.rcWork.left&&logLimits.ptMinTrackSize.y<=logMonitor.rcWork.bottom-logMonitor.rcWork.top,"Log minimum track size exceeds work area");
        for(int logicalWidth:{460,620,1000}){
            SetWindowPos(rendererLogWindow,nullptr,0,0,theme::px(logicalWidth),theme::px(420),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);rendererLogLayout();
            RECT client{};GetClientRect(rendererLogWindow,&client);std::vector<RECT> footer;
            for(HWND h:{rendererLogFollow,GetDlgItem(rendererLogWindow,902),GetDlgItem(rendererLogWindow,903),GetDlgItem(rendererLogWindow,IDCANCEL)}){
                auto bounds=relativeRect(h,rendererLogWindow);ensure(contains(client,bounds),"Log footer control clipped on narrow layout");
                for(const auto &other:footer){RECT overlap{};ensure(!IntersectRect(&overlap,&bounds,&other),"Log footer controls overlap on narrow layout");}footer.push_back(bounds);
            }
        }
        ensure(rendererLogSnapshot.find(activeSessionToken)!=std::string::npos,"Live log did not show current session");
        {std::ofstream out(appDirectory/L"logs/renderer.log",std::ios::app);out<<"new-live-evidence\n";}
        SendMessageW(rendererLogWindow,WM_TIMER,1,0);
        ensure(rendererLogSnapshot.find("new-live-evidence")!=std::string::npos,"Live log did not refresh appended data");
        auto oldToken=activeSessionToken;activeSessionToken=session::prepare(appDirectory/L"logs","second-live-log-fixture");
        SendMessageW(rendererLogWindow,WM_TIMER,1,0);
        ensure(rendererLogSnapshot.find(oldToken)==std::string::npos&&rendererLogSnapshot.find(activeSessionToken)!=std::string::npos,"Live viewer kept the previous session text");
        SendMessageW(rendererLogWindow,WM_COMMAND,IDCANCEL,0);ensure(!rendererLogWindow,"Log viewer did not close");
        SetEvent(child);CloseHandle(child);child=nullptr;
        std::cout<<"PASS: live log append refresh, session replacement and close\n";
        // Avoid normal app teardown/reopening/tray behavior in the test process.
        NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=window;tray.uID=1;Shell_NotifyIconW(NIM_DELETE,&tray);
        SetWindowLongPtrW(window,GWLP_WNDPROC,LONG_PTR(DefWindowProcW));DestroyWindow(window);theme::shutdown();CoUninitialize();
        std::cout<<"PASS: main launcher repaint regression, no simulator or network access\n";return 0;
    } catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
