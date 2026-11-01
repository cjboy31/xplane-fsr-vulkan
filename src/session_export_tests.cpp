// SPDX-License-Identifier: MIT
#include "session_export.hpp"
#include <iostream>
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int wmain(int argc,wchar_t**argv){try{
    if(argc==3&&std::wstring(argv[1])==L"--child"){Sleep(700);return std::stoi(argv[2]);}
    check(argc==2,"Fresh fixture required");const std::filesystem::path root=argv[1],sim=root/L"sim";
    check(!std::filesystem::exists(root),"Fixture already exists");std::filesystem::create_directories(sim/L"Resources");std::filesystem::create_directories(graphics::prefs(sim));std::filesystem::create_directories(root/L"logs");
    graphics::write(sim/L"Resources/settings.txt","SETTING_1 renopt_MSAA 0 hdr/msaa_hw 0\nSETTING_1 renopt_FXAA 0 hdr/use_post_aa 0\nSETTING_1 renopt_FSR_04 2 fsr/enable 1 fsr/quality 2\n");
    const auto prf=graphics::prefs(sim)/L"X-Plane.prf";
    const auto original=std::string("I\n10 Version\nrenopt_MSAA 2\nrenopt_FXAA 1\nrenopt_FSR_04 4\ncontroller untouched\n");graphics::write(prf,original);
    launcher::Preferences p;p.simulator=sim.wstring();p.mode=3;launcher::save(root/L"settings.ini",p);xp_renderer::save(sim,xp_renderer::Choice::fsr);
    check(!app::synchronizeInput(root).empty(),"Native AA did not prepare the saved input slider");
    const auto native=graphics::read(prf);check(graphics::get(native,"renopt_FSR_04")=="0"&&graphics::get(native,"renopt_FXAA")=="0"&&graphics::get(native,"renopt_MSAA")=="2"&&graphics::get(native,"controller")=="untouched","Managed input changed unrelated graphics or controller settings");
    check(app::synchronizeInput(root).empty(),"Unchanged input created a backup/write");
    p.mode=4;p.manual.scale=2;launcher::save(root/L"settings.ini",p);app::synchronizeInput(root);check(graphics::current(sim).scale==2,"Upscaling input was not prepared");
    xp_renderer::save(sim,xp_renderer::Choice::native_aa);app::synchronizeInput(root);check(graphics::current(sim).scale==0,"Independent AA did not select Full input");
    xp_renderer::save(sim,xp_renderer::Choice::fsr);p.mode=4;p.manualApplied=true;p.manual.scale=0;launcher::save(root/L"settings.ini",p);
    app::synchronizeInput(root);check(graphics::current(sim).scale==0,"An explicitly applied Full input was reset");
    xp_renderer::save(sim,xp_renderer::Choice::off);graphics::write(prf,original);check(app::synchronizeInput(root).empty()&&graphics::read(prf)==original,"Original simulator choice was overridden");
    xp_renderer::save(sim,xp_renderer::Choice::fsr);p.manualPending=true;launcher::save(root/L"settings.ini",p);check(app::synchronizeInput(root).empty()&&graphics::read(prf)==original,"Pending edits were overwritten");
    auto exe=sim/L"X-Plane.exe";std::filesystem::copy_file(app::processExecutable(),exe);
    auto launch=[&](int code){std::wstring command=L"\""+exe.wstring()+L"\" --child "+std::to_wstring(code);STARTUPINFOW s{sizeof(s)};PROCESS_INFORMATION pi{};check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,sim.c_str(),&s,&pi),"Cannot create inert simulator fixture");CloseHandle(pi.hThread);return pi;};
    std::wstring firstToken;
    for(int code:{0,93}){
        auto id=session::prepare(root/L"logs","fixture");const std::wstring token(id.begin(),id.end());
        if(!code)firstToken=token;
        auto header=session::header(id,"fixture");if(code)for(size_t pos=0;(pos=header.find('\n',pos))!=std::string::npos;pos+=2)header.insert(pos,1,'\r');
        session::write(root/L"logs/renderer.log",header+"current frame 123\n");
        auto process=launch(code);app::finishSession(root,process.dwProcessId,token);CloseHandle(process.hProcess);
        const auto latest=graphics::read(app::userLogDirectory(sim)/L"renderer.log");
        check(latest.rfind("XPFSR24 session="+id,0)==0&&latest.find("current frame 123")!=std::string::npos&&latest.find("process_exit="+std::to_string(code))!=std::string::npos,"Normal/crash session export did not match its log");
    }
    const auto latest=app::userLogDirectory(sim)/L"renderer.log";const auto before=graphics::read(latest);
    check(!app::exportSessionLog(root,sim,firstToken,0)&&graphics::read(latest)==before,"A late export replaced a newer session");
    check(!app::exportSessionLog(root,sim,L"ffffffffffffffffffffffffffffffff",0)&&graphics::read(latest)==before,"Wrong token exported stale diagnostics");
    check(std::filesystem::is_regular_file(app::userLogDirectory(sim)/L"renderer.previous.log"),"Previous session was lost");
    graphics::write(latest,"Keep this user file");auto id=session::id();session::write(root/L"logs/renderer.log",session::header(id,"fixture"));bool refused=false;
    try{app::exportSessionLog(root,sim,std::wstring(id.begin(),id.end()),0);}catch(...){refused=true;}
    check(refused&&graphics::read(latest)=="Keep this user file","Unrecognized log destination was overwritten");
    std::cout<<"PASS: input/mode/renderer matching, unchanged MSAA/controller settings, pending/off/no-op preservation, normal and abnormal exit log export, wrong-session refusal and previous/unknown-file preservation.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
