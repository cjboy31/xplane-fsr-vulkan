// SPDX-License-Identifier: MIT
// Hidden native fixture for the production update-only entry point.
#define wWinMain retiredUpdaterEntry
#include "plugin_updater.cpp"
#undef wWinMain
#include <iostream>
int wmain(int argc,wchar_t**argv){try{
    ensure(argc==2,"Fresh fixture required");appDirectory=argv[1];ensure(!fs::exists(appDirectory),"Fixture exists");fs::create_directories(appDirectory);
    auto settings=appDirectory/L"settings.ini";
    graphics::write(settings,"[Launcher]\nMode=4\nFrameGeneration=2\nPreset=fsr4-4k-quality\n[NeuralRendering]\nNRPasses=1\nNRIntensity=35\n[Updates]\nAutomaticChecks=0\n[Unknown]\nKeep=this\n");
    const auto saved=graphics::read(settings);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSW c{};c.lpfnWndProc=updaterProcedure;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"UpdaterOnlyHiddenFixture";RegisterClassW(&c);
    auto h=CreateWindowExW(0,c.lpszClassName,L"Hidden updater test",0,0,0,690,492,nullptr,nullptr,c.hInstance,nullptr);ensure(h!=nullptr,"Cannot create updater fixture");
    ensure(GetDlgItem(h,1)&&GetDlgItem(h,2)&&GetDlgItem(h,3)&&GetDlgItem(h,4),"Updater actions missing");
    ensure(!GetDlgItem(h,20)&&!GetDlgItem(h,21),"Retired launch or rendering controls are present");
    updaterProcedure(h,WM_COMMAND,20,0);saveChoices();refreshUpdateUI();
    ensure(graphics::read(settings)==saved,"Updater rewrote live rendering choices");
    updaterProcedure(h,WM_CLOSE,0,0);ensure(!IsWindow(h),"Updater did not close");theme::shutdown();CoUninitialize();
    std::cout<<"PASS: update-only native controls, retired launch command ignored, settings preserved and clean close; no desktop input or network.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
