// SPDX-License-Identifier: MIT
#include "implicit_activation.hpp"
#include "layer_registration.hpp"
#include "deferred_graphics.hpp"
#include "plugin_install.hpp"
#include "installer_cleanup.hpp"
#include <iostream>
#include <future>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool failed=false;try{f();}catch(...){failed=true;}check(failed,"Unsafe configuration was accepted");}
int wmain(int argc,wchar_t **argv){try{
    check(argc==2,"Fresh test root required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Refusing an existing fixture");
    std::filesystem::create_directories(root/L"sim");graphics::write(root/L"sim/X-Plane.exe","fixture");
    std::filesystem::create_directories(root/L"other-sim");graphics::write(root/L"other-sim/X-Plane.exe","different fixture");
    launcher::Preferences p;p.simulator=(root/L"sim").wstring();p.mode=4;p.frameGeneration=3;p.cockpitRestore=false;p.neuralRendering=4;p.nr[xpfsr::nr::Enabled]=1;p.nr[xpfsr::nr::AllowFG]=1;
    launcher::save(root/L"settings.ini",p);
    std::filesystem::create_directories(root/L"plugin/64");
    graphics::write(root/L"plugin/64/win.xpl","owned fixture plugin");
    const auto installedPlugin=root/L"sim/Resources/plugins/XPFSR24/64/win.xpl";
    app::installPlugin(root,root/L"sim");
    check(app::fileHash(installedPlugin)==app::fileHash(root/L"plugin/64/win.xpl"),"Integration did not install its plugin");
    graphics::write(root/L"plugin/64/win.xpl","updated fixture plugin");
    app::installPlugin(root,root/L"sim");
    check(app::fileHash(installedPlugin)==app::fileHash(root/L"plugin/64/win.xpl"),"Integration update did not recognize its recorded plugin");
    graphics::write(installedPlugin,"unrelated edited plugin");
    rejects([&]{app::installPlugin(root,root/L"sim");});
    check(!app::removeOwnedPlugin(root,root/L"sim")&&graphics::read(installedPlugin)=="unrelated edited plugin","Uninstall removed an unrelated plugin");
    std::filesystem::remove(installedPlugin);app::installPlugin(root,root/L"sim");
    graphics::write(installedPlugin.parent_path()/L"user.txt","keep user file");
    check(app::removeOwnedPlugin(root,root/L"sim")&&!std::filesystem::exists(installedPlugin)&&graphics::read(installedPlugin.parent_path()/L"user.txt")=="keep user file","Owned plugin removal lost unrelated files");
    check(!app::removeOwnedPlugin(root,root/L"disconnected")&&!app::removeOwnedPlugin(root,{}),"Missing simulator cleanup failed");
    std::filesystem::create_directories(root/L"media");
    graphics::write(root/L"media/0.7.1-alpha.mp4","previous movie");
    const auto previousMovie=app::fileHash(root/L"media/0.7.1-alpha.mp4");
    launcher::writeSetting(L"Video",L"Version",L"0.7.1-alpha",(root/L"media/feature-video.ini").c_str());
    const std::wstring previousDigest(previousMovie.begin(),previousMovie.end());
    check(!app::retirePreviousGuide(root,L"0.7.1-alpha",previousDigest),"Same-version repair deleted the current guide");
    launcher::writeSetting(L"Video",L"Version",L"0.7.2-alpha",(root/L"media/feature-video.ini").c_str());
    check(!app::retirePreviousGuide(root,L"0.7.1-alpha",std::wstring(64,L'0')),"Changed/unowned guide was deleted");
    rejects([&]{app::retirePreviousGuide(root,L"../outside",previousDigest);});
    check(app::retirePreviousGuide(root,L"0.7.1-alpha",previousDigest)&&!std::filesystem::exists(root/L"media/0.7.1-alpha.mp4"),"Verified previous guide was retained");
    auto protectedProfile=app::automaticCaptureProfile(p,true);check(protectedProfile.frameGeneration==0&&p.frameGeneration==3&&protectedProfile.mode==p.mode&&protectedProfile.neuralRendering==p.neuralRendering,"Capture guard changed saved choices or other processing");
    check(app::automaticCaptureProfile(p,false).frameGeneration==3,"Capture-free profile lost FG");
    auto pending=p;pending.manualPending=true;pending.mode=3;
    auto running=app::runningProfile(pending,graphics::Options{1,0,2,0,0},false);
    check(running.mode==4&&!running.manualPending&&running.frameGeneration==3&&pending.mode==3&&pending.manualPending,"Pending graphics disabled FSR, reset FG or changed saved intent");
    check(app::runningProfile(pending,graphics::Options{},false).mode==3,"Current full scale did not choose Native AA while graphics are pending");
    // Regression: a cloud/native-AA choice can outlive the pending flag, or
    // X-Plane/another launcher can independently change the source scale.
    for(int mode=0;mode<=4;++mode)for(int scale=0;scale<=4;++scale)for(bool queued:{false,true}){
        auto saved=p;saved.mode=mode;saved.manualPending=queued;
        auto before=launcher::serialized(saved);auto actual=app::runningProfile(saved,graphics::Options{1,0,scale,0,0},false);
        const int expected=mode?(mode>=3?3:1)+(scale>0?1:0):0;
        check(actual.mode==expected&&!actual.manualPending&&actual.frameGeneration==saved.frameGeneration&&actual.neuralRendering==saved.neuralRendering&&actual.nr.value==saved.nr.value,"Actual input adaptation changed provider family, NR or FG");
        check(launcher::serialized(saved)==before,"Runtime adaptation mutated saved intent");
        auto notice=app::runningGraphicsNotice(saved,actual);
        check(!notice.empty()==(queued||mode!=expected),"Runtime adaptation did not explain its effective mode");
        check(app::runningProfile(saved,graphics::Options{1,0,scale,0,0},true).frameGeneration==0,"Input adaptation bypassed capture guard");
    }
    for(int mode:{-1,5}){auto invalid=p;invalid.mode=mode;rejects([&]{app::validateSessionProfile(app::runningProfile(invalid,graphics::Options{},false));});}
    for(auto name:{L"DISABLE_XPFSR24",L"VK_INSTANCE_LAYERS",L"XPFSR24_RUNTIME",L"XPFSR24_SETTINGS"})SetEnvironmentVariableW(name,nullptr);
    check(!app::automaticHost(root,root/L"sim/X-Plane.exe"),"Activation enabled without opt-in");
    launcher::writeSetting(L"InSim",L"Enabled",L"1",(root/L"settings.ini").c_str());
    check(app::automaticHost(root,root/L"sim/X-Plane.exe"),"Selected simulator did not qualify");
    check(!app::automaticHost(root,root/L"other-sim/X-Plane.exe")&&!app::automaticHost(root,root/L"missing/X-Plane.exe"),"Filename-only or disconnected simulator activated");
    SetEnvironmentVariableW(L"DISABLE_XPFSR24",L"1");check(!app::automaticHost(root,root/L"sim/X-Plane.exe"),"Native comparison bypass failed");SetEnvironmentVariableW(L"DISABLE_XPFSR24",nullptr);
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS",L"OTHER;VK_LAYER_XPFSR24_SUFFIX");SetEnvironmentVariableW(L"XPFSR24_RUNTIME",root.c_str());check(!app::explicitHost(),"Partial explicit layer name accepted");
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS",L"OTHER;VK_LAYER_XPFSR24");check(app::explicitHost(),"Intentional explicit fixture stopped working");
    SetEnvironmentVariableW(L"XPFSR24_SETTINGS",(root/L"settings.ini").c_str());check(!app::explicitHost(),"Inherited simulator-only profile activated a different executable");
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS",nullptr);SetEnvironmentVariableW(L"XPFSR24_SETTINGS",nullptr);
    // Exercise the real FSR activation entry, before and after Native AA claims.
    for(bool aaFirst:{false,true}){
        for(auto name:{"XP_RENDERER_SESSION_PID","XP_RENDERER_SESSION_CHOICE","XP_RENDERER_OWNER","XP_RENDERER_REQUEST","DISABLE_XPFSR24","XPFSR24_AUTOMATIC","XPFSR24_RUNTIME"})xp_renderer::set(name,{});
        xp_renderer::save(root/L"sim",xp_renderer::Choice::native_aa);
        if(aaFirst)check(xp_renderer::claim(root/L"sim",xp_renderer::Choice::native_aa).allowed,"AA startup claim failed");
        app::Activation competing;competing.initialize(root,root/L"sim/X-Plane.exe");
        check(!competing.active&&app::environment(L"XPFSR24_AUTOMATIC").empty(),"FSR prepared a renderer in an AA session");
        check(xp_renderer::claim(root/L"sim",xp_renderer::Choice::native_aa).allowed,"FSR-first negotiation blocked selected AA");
    }
    for(auto name:{"XP_RENDERER_SESSION_PID","XP_RENDERER_SESSION_CHOICE","XP_RENDERER_OWNER","XP_RENDERER_REQUEST","DISABLE_XPFSR24","XPFSR24_AUTOMATIC","XPFSR24_RUNTIME"})xp_renderer::set(name,{});
    xp_renderer::save(root/L"sim",xp_renderer::Choice::fsr);
    // A selected simulator with invalid settings still gets fresh diagnostics.
    // Its previous session is retained, and another session cannot erase it.
    std::filesystem::create_directories(root/L"logs");graphics::write(root/L"logs/renderer.log","previous session sentinel");
    app::Activation failed;failed.initialize(root,root/L"sim/X-Plane.exe");check(!failed.active&&!failed.error.empty()&&graphics::read(root/L"logs/renderer.log.previous")=="previous session sentinel"&&graphics::read(root/L"logs/renderer.log").find("Renderer inactive:")!=std::string::npos,"Failed activation did not retain old diagnostics and write current bypass reason");
    check(app::validSessionToken(app::environment(L"XPFSR24_CONFIG_SESSION"))&&!app::environment(L"XPFSR24_ACTIVATION_ERROR").empty(),"Failed activation left no configuration-only panel path");
    auto firstLog=graphics::read(root/L"logs/renderer.log");app::Activation concurrent;concurrent.initialize(root,root/L"sim/X-Plane.exe");check(!concurrent.active&&graphics::read(root/L"logs/renderer.log")==firstLog,"Second session erased the first session's diagnostics");
    auto invalid=p;invalid.nr[xpfsr::nr::AllowFG]=0;rejects([&]{app::validateSessionProfile(invalid);});invalid=p;invalid.cockpitRestore=true;rejects([&]{app::validateSessionProfile(invalid);});invalid=p;invalid.mode=0;rejects([&]{app::validateSessionProfile(invalid);});
    xpfsr::nr::ControlChannel channel,reader;auto token=app::prepareSessionProfile(root,p,channel);xpfsr::nr::Controls live;
    check(token.size()==32&&reader.open(app::environment(L"XPFSR24_NR_CONTROL").c_str())&&reader.read(live)&&live[xpfsr::nr::Enabled]&&live[xpfsr::nr::AllowFG],"Shared live controls missing");
    check(app::environment(L"XPFSR24_PROVIDER")==L"4.1.2"&&app::environment(L"XPFSR24_FG")==L"4.0.2"&&app::environment(L"XPFSR24_FG_PRESENTER")==L"3.1.7"&&app::environment(L"XPFSR24_FG_SYNCHRONIZED")==L"1","Provider/compatibility mapping changed");
    check(graphics::read(root/L"logs/renderer.log.previous")==firstLog&&graphics::read(root/L"logs/renderer-control.txt")=="1","Fresh logs/processing configuration failed");
    auto native=app::prepareSessionProfile(root,p,channel,true);check(native!=token&&app::environment(L"XPFSR24_NR_CONTROL").empty()&&app::environment(L"XPFSR24_FG").empty()&&app::environment(L"XPFSR24_MODE")==L"observe","Native session retained renderer requests");
    auto registry=L"Software\\XPFSR24\\Tests\\Activation-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    struct Cleanup{std::wstring key;~Cleanup(){RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());}}cleanup{registry};
    const auto layers=registry+L"\\Layers",owner=registry+L"\\Owner";
    app::LayerRegistration registration(HKEY_CURRENT_USER,layers,owner);
    for(auto dir:{root,root/L"second"}){std::filesystem::create_directories(dir/L"auto");std::filesystem::create_directories(dir/L"layer");graphics::write(dir/L"auto/XPFSR24Implicit.json","fixture");graphics::write(dir/L"layer/XPFSR24Layer.dll","fixture");launcher::save(dir/L"settings.ini",p);}
    HKEY key{};check(RegCreateKeyExW(HKEY_CURRENT_USER,layers.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"Fixture registry setup");DWORD value=7;RegSetValueExW(key,L"Other-layer.json",0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value));RegCloseKey(key);
    registration.enable(root);DWORD enabled=99,bytes=sizeof(enabled);check(RegGetValueW(HKEY_CURRENT_USER,layers.c_str(),app::LayerRegistration::manifest(root).c_str(),RRF_RT_REG_DWORD,nullptr,&enabled,&bytes)==ERROR_SUCCESS&&enabled==0,"Implicit registration must use DWORD zero");
    registration.repair(root);
    launcher::writeSetting(L"InSim",L"Enabled",L"0",(root/L"settings.ini").c_str());rejects([&]{registration.repair(root);});
    check(launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0","Registration repair re-enabled an opted-out installation");registration.enable(root);
    std::future<std::string> racingRepair;std::promise<void> repairStarted;
    {app::NamedLock guard(app::lockName(owner));
        racingRepair=std::async(std::launch::async,[&]{repairStarted.set_value();try{registration.repair(root);return std::string{};}catch(const std::exception&e){return std::string(e.what());}});
        repairStarted.get_future().wait();Sleep(50);
        launcher::writeSetting(L"InSim",L"Enabled",L"0",(root/L"settings.ini").c_str());
    }
    check(racingRepair.get().find("disabled")!=std::string::npos&&launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0","Racing registration repair reversed an opt-out");registration.enable(root);
    launcher::writeSetting(L"InSim",L"Enabled",L"1",(root/L"second/settings.ini").c_str());rejects([&]{registration.repair(root/L"second");});
    rejects([&]{registration.enable(root/L"second");});registration.disable(root/L"second");check(!_wcsicmp(app::registryString(HKEY_CURRENT_USER,owner.c_str(),L"Manifest").c_str(),app::LayerRegistration::manifest(root).c_str()),"Another installation removed owner");
    // Removal works without a connected simulator and leaves unrelated layers.
    std::filesystem::remove(root/L"sim/X-Plane.exe");registration.disable(root);bytes=sizeof(value);check(RegGetValueW(HKEY_CURRENT_USER,layers.c_str(),L"Other-layer.json",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS&&value==7,"Unrelated Vulkan layer changed");
    check(app::registryString(HKEY_CURRENT_USER,owner.c_str(),L"Manifest").empty()&&launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0","Disable left activation owned/enabled");
    std::cout<<"PASS: exact selected executable, opt-in/disable, inherited-profile rejection, fail-closed logs, shared provider/session mapping, registry ownership and disconnected-drive cleanup.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
