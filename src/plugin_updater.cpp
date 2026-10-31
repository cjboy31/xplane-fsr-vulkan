// SPDX-License-Identifier: MIT
// Update-only replacement for the retired desktop rendering dashboard.
#include "graphics_settings.hpp"
#include "launcher_preferences.hpp"
#include "launcher_theme.hpp"
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
#include "update_ui.inc"
static LRESULT CALLBACK updaterProcedure(HWND h,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{window=h;theme::initialize(h);font=theme::fonts[0];
        scaledControl(L"STATIC",L"X-Plane Plugin Updater",0,24,24,640,32);
        scaledControl(L"STATIC",L"Change image settings inside X-Plane. This app only helps you keep your plugins up to date.",0,24,72,640,54);
        updatesButton=scaledControl(L"BUTTON",L"FSR updates / release channel",BS_PUSHBUTTON|WS_TABSTOP,24,146,640,42,1);
        scaledControl(L"BUTTON",L"Native AA updates",BS_PUSHBUTTON|WS_TABSTOP,24,202,640,42,2);
        scaledControl(L"STATIC",L"Check, download and install verified FSR and Native AA updates.\r\n\r\nClose X-Plane before installing either plugin. Your saved settings are kept. Use your usual simulator launcher afterward.",0,24,266,640,140);
        scaledControl(L"BUTTON",L"Where are my settings?",BS_PUSHBUTTON|WS_TABSTOP,24,424,310,38,3);
        scaledControl(L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,354,424,310,38,4);
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
        case 3:MessageBoxW(h,L"Launch X-Plane normally, then open Plugins > FSR 3/4 + Frame Gen > Open settings, or Plugins > Native AA > Open AA controls.\r\n\r\nUse Changelog in either panel to read changes. The old desktop rendering controls and launch video have been retired. Your settings have not been reset.",L"Your controls are in X-Plane",MB_OK|MB_ICONINFORMATION);break;
        case 4:SendMessageW(h,WM_CLOSE,0,0);break;}}
        catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Plugin updater",MB_OK|MB_ICONERROR);}return 0;
    case WM_ERASEBKGND:{RECT area;GetClientRect(h,&area);FillRect(reinterpret_cast<HDC>(w),&area,theme::backgroundBrush);return 1;}
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
    theme::dpi=theme::systemDpi();RECT bounds{0,0,theme::px(690),theme::px(492)};AdjustWindowRectEx(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
    auto h=CreateWindowExW(0,klass.c_str(),L"X-Plane Plugin Updater",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!h){CloseHandle(lease);CoUninitialize();return 1;}ShowWindow(h,show);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(GetActiveWindow(),&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    theme::shutdown();CloseHandle(lease);CoUninitialize();return int(m.wParam);
}
