// SPDX-License-Identifier: MIT
// Update-only replacement for the retired desktop rendering dashboard.
#include "graphics_settings.hpp"
#include "launcher_preferences.hpp"
#include "launcher_theme.hpp"
#include "product_names.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <shellapi.h>
namespace fs=std::filesystem;
static fs::path appDirectory;
static HWND window{};
static HFONT font{};
static HANDLE aaChild{};
static bool running(){return false;}
static bool nrJobRunning(){return false;}
// Updates must never write cached rendering choices over live in-sim changes.
static void saveChoices(){}
static void ensure(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::wstring wide(const char* text){int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);std::wstring s(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,text,-1,s.data(),n);if(n)s.pop_back();return s;}
static std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
static HWND scaledControl(const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0,HWND parent=nullptr){
    auto result=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style|(std::wstring(type)==L"COMBOBOX"?CBS_OWNERDRAWFIXED|CBS_HASSTRINGS:0),theme::px(x),theme::px(y),theme::px(w),theme::px(h),parent?parent:window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(result,WM_SETFONT,WPARAM(font),TRUE);theme::apply(result,type);return result;
}
static SIZE modalSize(int w,int h){return {theme::px(w),theme::px(h)};}
static HWND updaterLabel(const wchar_t* value,int x,int y,int w,int h,int face=0,bool card=false){
    auto control=scaledControl(L"STATIC",value,0,x,y,w,h);
    theme::face(control,face);theme::surface(control,card?0:1);
    return control;
}
#define XPFSR_UPDATER_ONLY 1
#include "update_ui.inc"
static LRESULT CALLBACK updaterProcedure(HWND h,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{window=h;theme::initialize(h);font=theme::fonts[0];
        auto icon=scaledControl(L"STATIC",L"",SS_ICON,26,28,42,42);
        theme::surface(icon,1);
        SendMessageW(icon,STM_SETICON,WPARAM(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,theme::px(40),theme::px(40),LR_SHARED)),0);
        updaterLabel(L"Plugin Updater",82,24,540,38,5);
        theme::tone(updaterLabel(L"Keep your X-Plane plugins up to date.",82,68,540,28),theme::muted);
        theme::tone(updaterLabel(L"YOUR PLUGINS",28,112,600,20,2),theme::gold);
        updaterLabel(L"FSR 3.1 / 4.1 + Frame Gen",44,158,356,28,3,true);
        theme::tone(updaterLabel(L"Smoother edges, upscaling and\r\noptional neural rendering and extra frames.",44,196,364,46,0,true),theme::muted);
        updatesButton=scaledControl(L"BUTTON",L"FSR updates / channel",BS_PUSHBUTTON|WS_TABSTOP,430,183,214,46,1);
        updaterLabel(L"Native AA",44,294,356,28,3,true);
        theme::tone(updaterLabel(L"SMAA, FXAA and experimental TAA.\r\nAnti-aliasing at your normal resolution.",44,332,364,46,0,true),theme::muted);
        scaledControl(L"BUTTON",L"Native AA updates",BS_PUSHBUTTON|WS_TABSTOP,430,319,214,46,2);
        theme::tone(updaterLabel(L"Close X-Plane to install. Your saved choices are kept.",28,418,640,24),theme::muted);
        theme::tone(updaterLabel(L"Change image settings in the simulator. Use your usual launcher.",28,448,640,24),theme::muted);
        scaledControl(L"BUTTON",L"Find my in-sim controls",BS_PUSHBUTTON|WS_TABSTOP,28,488,400,40,3);
        scaledControl(L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,450,488,214,40,4);
        autoCheckUpdates=launcher::readSetting(appDirectory/L"settings.ini",L"Updates",L"AutomaticChecks",L"1")!=L"0";
        releaseChannel=updates::loadChannel(appDirectory/L"settings.ini");SetTimer(h,4,400,nullptr);if(autoCheckUpdates)PostMessageW(h,WM_APP+1,0,0);return 0;}
    case WM_APP+1:startUpdateCheck();return 0;
    case WM_TIMER:pollUpdate();return 0;
    case WM_COMMAND:
        try{switch(LOWORD(w)){
        case 1:showUpdates();break;
        case 2:{auto helper=appDirectory/L"NativeAAUpdater.exe";ensure(fs::is_regular_file(helper),"The Native AA update component is missing. Reinstall the complete package.");
            auto sim=launcher::load(appDirectory/L"settings.ini").simulator;
            auto args=L"--simulator \""+sim+L"\"";
            ensure(reinterpret_cast<INT_PTR>(ShellExecuteW(h,L"open",helper.c_str(),args.c_str(),appDirectory.c_str(),SW_SHOWNORMAL))>32,"Could not open Native AA updates.");break;}
        case 3:MessageBoxW(h,L"Launch X-Plane normally, then open Plugins > FSR 3.1 / 4.1 + Frame Gen > Open settings, or Plugins > Native AA > Open AA controls.\r\n\r\nUse Changelog in either panel to read changes. Your saved settings are kept.",L"Your controls are in X-Plane",MB_OK|MB_ICONINFORMATION);break;
        case 4:SendMessageW(h,WM_CLOSE,0,0);break;}}
        catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Plugin updater",MB_OK|MB_ICONERROR);}return 0;
    case WM_ERASEBKGND:{RECT area;GetClientRect(h,&area);FillRect(reinterpret_cast<HDC>(w),&area,theme::backgroundBrush);return 1;}
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT area;GetClientRect(h,&area);FillRect(dc,&area,theme::backgroundBrush);
        for(int top:{144,280}){RECT card{theme::px(28),theme::px(top),theme::px(664),theme::px(top+116)};theme::rounded(dc,card,theme::card,theme::border,14);}EndPaint(h,&paint);return 0;}
    case WM_DRAWITEM:if(theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT*>(l)))return TRUE;break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:return theme::colors(message,w,l);
    case WM_CLOSE:{std::lock_guard<std::mutex> lock(updateJob->mutex);updateJob->cancel=true;DestroyWindow(h);return 0;}
    case WM_DESTROY:KillTimer(h,4);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);theme::enableDpi();
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);appDirectory=fs::path(self).parent_path();
    const auto klass=L"XPFSR24Launcher_"+std::to_wstring(std::hash<std::wstring>{}(appDirectory.wstring()));
    auto lease=CreateFileW((appDirectory/L"launcher.lock").c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(lease==INVALID_HANDLE_VALUE){if(auto old=FindWindowW(klass.c_str(),nullptr)){ShowWindow(old,SW_RESTORE);SetForegroundWindow(old);}CoUninitialize();return 0;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW c{};c.lpfnWndProc=updaterProcedure;c.hInstance=instance;c.lpszClassName=klass.c_str();c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&c);
    theme::dpi=theme::systemDpi();RECT bounds{0,0,theme::px(690),theme::px(552)};AdjustWindowRectEx(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,FALSE,0);
    auto h=CreateWindowExW(0,klass.c_str(),L"X-Plane Plugin Updater",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!h){CloseHandle(lease);CoUninitialize();return 1;}ShowWindow(h,show);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(GetActiveWindow(),&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    theme::shutdown();CloseHandle(lease);CoUninitialize();return int(m.wParam);
}
