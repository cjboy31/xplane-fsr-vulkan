// SPDX-License-Identifier: MIT
#include "deferred_graphics.hpp"
#include <iostream>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int wmain(int argc,wchar_t **argv){try{
    if(argc==3&&std::wstring(argv[1])==L"--child"){Sleep(1000);return std::stoi(argv[2]);}
    check(argc==2,"Fresh fixture root required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Fixture exists");
    auto sim=root/L"sim",file=root/L"settings.ini";std::filesystem::create_directories(sim/L"Resources");std::filesystem::create_directories(graphics::prefs(sim));std::filesystem::create_directories(root/L"logs");
    graphics::write(sim/L"Resources/settings.txt","SETTING_1 renopt_MSAA 0 hdr/msaa_hw 0\nSETTING_1 renopt_FXAA 0 hdr/use_post_aa 0\nSETTING_1 renopt_FSR_04 2 fsr/enable 1 fsr/quality 2\n");
    auto main=graphics::prefs(sim)/L"X-Plane.prf",window=graphics::prefs(sim)/L"X-Plane Window Positions.prf";
    graphics::write(main,"I\n10 Version\nrenopt_MSAA 1\nrenopt_FXAA 0\nrenopt_FSR_04 0\nuntouched keep-me\n");
    const std::string windows="I\n10 Version\nnum_monitors 1\nmonitor/0/m_monitor 0\nmonitor/0/m_x_res_full 2560\nmonitor/0/m_y_res_full 1440\nmonitor/0/m_is_fullscreen wmgr_mode_windowed\n";graphics::write(window,windows);
    launcher::Preferences p;p.simulator=sim.wstring();p.mode=4;p.frameGeneration=2;p.cockpitRestore=false;p.manual={2,0,2,0,0};p.manualPending=true;launcher::save(file,p);
    auto exe=sim/L"X-Plane.exe";std::filesystem::copy_file(app::processExecutable(),exe);
    const std::wstring token=L"0123456789abcdef0123456789abcdef";
    auto launch=[&](int code){std::wstring command=L"\""+exe.wstring()+L"\" --child "+std::to_wstring(code);STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,sim.c_str(),&startup,&process),"Cannot create inert simulator process");CloseHandle(process.hThread);return process;};
    launcher::writeSetting(L"InSim",L"GraphicsQueued",token.c_str(),file.c_str());auto process=launch(0);
    app::waitAndApplyGraphics(root,process.dwProcessId,token);CloseHandle(process.hProcess);
    auto saved=launcher::load(file);check(!saved.manualPending&&saved.manualApplied&&saved.mode==4&&saved.frameGeneration==2,"Deferred apply reset providers or pending state");
    check(graphics::get(graphics::read(main),"renopt_MSAA")=="2"&&graphics::get(graphics::read(main),"renopt_FSR_04")=="2"&&graphics::get(graphics::read(main),"untouched")=="keep-me","Deferred apply changed wrong settings");
    check(graphics::read(window)==windows,"Keep output changed simulator window geometry");check(!app::applyQueuedGraphics(root,token),"Queued graphics applied twice");
    p=saved;p.manual.msaa=3;p.manualPending=true;launcher::save(file,p);launcher::writeSetting(L"InSim",L"GraphicsQueued",token.c_str(),file.c_str());auto before=graphics::read(main);process=launch(93);bool refused=false;try{app::waitAndApplyGraphics(root,process.dwProcessId,token);}catch(...){refused=true;}CloseHandle(process.hProcess);check(refused&&graphics::read(main)==before&&launcher::load(file).manualPending,"Abnormal exit changed graphics or lost request");
    check(!app::applyQueuedGraphics(root,L"ffffffffffffffffffffffffffffffff")&&graphics::read(main)==before,"Wrong session applied graphics");
    std::cout<<"PASS: normal-exit deferred graphics, provider preservation, unchanged window/UI, exact backup, single apply, wrong-session and crash refusal.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
