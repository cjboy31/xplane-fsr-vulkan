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
        child=CreateEventW(nullptr,TRUE,FALSE,nullptr);refresh();
        ensure(!IsWindowEnabled(modeBox)&&!IsWindowEnabled(presetBox),
               "Runtime control gating lost reparented controls");
        SetEvent(child);CloseHandle(child);child=nullptr;refresh();ensure(IsWindowEnabled(modeBox)&&IsWindowEnabled(presetBox),"Controls did not unlock");
        // Avoid normal app teardown/reopening/tray behavior in the test process.
        NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=window;tray.uID=1;Shell_NotifyIconW(NIM_DELETE,&tray);
        SetWindowLongPtrW(window,GWLP_WNDPROC,LONG_PTR(DefWindowProcW));DestroyWindow(window);theme::shutdown();CoUninitialize();
        std::cout<<"PASS: main launcher repaint regression, no simulator or network access\n";return 0;
    } catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
