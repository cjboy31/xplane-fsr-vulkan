// SPDX-License-Identifier: MIT
#include "implicit_activation.hpp"
#include "layer_registration.hpp"
#include "deferred_graphics.hpp"
#include "plugin_install.hpp"
#include "reconstruction_plugins.hpp"
#include "installer_cleanup.hpp"
#include <iostream>
#include <future>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool failed=false;try{f();}catch(...){failed=true;}check(failed,"Unsafe configuration was accepted");}
int wmain(int argc,wchar_t **argv){try{
    check(argc==2,"Fresh test root required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Refusing an existing fixture");
    std::filesystem::create_directories(root/L"sim");graphics::write(root/L"sim/X-Plane.exe","fixture");
    std::filesystem::create_directories(root/L"other-sim");graphics::write(root/L"other-sim/X-Plane.exe","different fixture");
    check(app::reconstructionConflict(root/L"sim").empty(),"Plain simulator incorrectly reported a reconstruction conflict");
    for(const auto* name:{L"LuminaXP",L"MotionVectors"}){
        const auto plugin=root/L"sim/Resources/plugins"/name;
        std::filesystem::create_directories(plugin/L"64");
        graphics::write(plugin/L"README.txt","support files are not an active plugin");
        graphics::write(plugin/L"64/lin.xpl","other platform");
        check(app::reconstructionConflict(root/L"sim").empty(),"Empty/support-only/other-platform folder caused a conflict");
        const std::filesystem::path candidates[]={plugin/L"64/win.xpl",plugin/L"win_x64"/(std::wstring(name)+L".xpl"),plugin/L"win.xpl",root/L"sim/Resources/plugins"/(std::wstring(name)+L".xpl")};
        for(const auto& file:candidates){
            std::filesystem::create_directories(file.parent_path());graphics::write(file,"Windows plugin fixture");
            const auto warning=app::reconstructionConflict(root/L"sim");
            check(warning.find(file.lexically_relative(root/L"sim").generic_u8string())!=std::string::npos&&warning.find("does not confirm")!=std::string::npos,"Conflict warning did not identify its file or distinguish presence from activity");
            check(app::reconstructionConflict(root/L"other-sim").empty(),"Another simulator's plugin caused a conflict");
            auto disabled=file;disabled+=L".disabled";std::filesystem::rename(file,disabled);
            check(app::reconstructionConflict(root/L"sim").empty(),"Disabled plugin file caused a conflict");
            check(graphics::read(disabled)=="Windows plugin fixture","Conflict inspection changed a plugin");std::filesystem::remove(disabled);
        }
    }
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
    graphics::write(root/L"media/0.7.1-alpha.mp4","previous movie");
    graphics::write(root/L"media/feature-video.ini","[Video]\nVersion=0.7.1-alpha\nSHA256="+previousMovie+"\n");
    check(app::retirePreviousGuide(root,L"0.7.1-alpha",previousDigest,true)&&!std::filesystem::exists(root/L"media/feature-video.ini"),"Retired video metadata was retained");
    std::filesystem::create_directories(root/L"media");graphics::write(root/L"media/TRANSCRIPT.txt","old owned guide text");graphics::write(root/L"media/user.txt","user change");
    const auto guideTextHash=app::fileHash(root/L"media/TRANSCRIPT.txt");
    graphics::write(root/L"old-payload.json","{\"product\":\"FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)\",\"files\":[{\"path\":\"media/TRANSCRIPT.txt\",\"sha256\":\""+guideTextHash+"\"},{\"path\":\"media/user.txt\",\"sha256\":\""+std::string(64,'0')+"\"}]}");
    check(app::retireOwnedMedia(root,root/L"old-payload.json")==1&&!std::filesystem::exists(root/L"media/TRANSCRIPT.txt")&&graphics::read(root/L"media/user.txt")=="user change","Old guide cleanup removed modified data or retained unchanged owned media");
    auto protectedProfile=app::automaticCaptureProfile(p,true);check(protectedProfile.frameGeneration==3&&p.frameGeneration==3&&protectedProfile.mode==p.mode&&protectedProfile.neuralRendering==p.neuralRendering,"Capture compatibility changed saved choices or other processing");
    for(int selection=0;selection<=4;++selection){auto saved=p;saved.frameGeneration=selection;auto running=app::automaticCaptureProfile(saved,true);
        check(running.frameGeneration==(selection==1?4:selection==2?3:selection),"Capture compatibility silently disabled or changed the FG algorithm");
        check(saved.frameGeneration==selection,"Capture compatibility rewrote saved FG");}
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
        check(app::runningProfile(saved,graphics::Options{1,0,scale,0,0},true).frameGeneration==3,"Input adaptation lost compatible frame generation");
    }
    auto screens=p;screens.cockpitRestore=true;screens.alignmentTint=true;
    auto compatibleScreens=app::runningProfile(screens,graphics::Options{},true);
    check(!compatibleScreens.cockpitRestore&&!compatibleScreens.alignmentTint&&screens.cockpitRestore&&screens.alignmentTint,"FG session screen protection changed saved choices or remained incompatible");
    screens.frameGeneration=0;check(app::runningProfile(screens,graphics::Options{},true).cockpitRestore,"FG-Off session unnecessarily lost cockpit screen protection");
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
    check(app::environment(L"XPFSR24_MOTION").empty(),"Camera-derived motion must not set a native request");
    check(graphics::read(root/L"logs/renderer.log.previous")==firstLog&&graphics::read(root/L"logs/renderer-control.txt")=="1","Fresh logs/processing configuration failed");
    auto native=app::prepareSessionProfile(root,p,channel,true);check(native!=token&&app::environment(L"XPFSR24_NR_CONTROL").empty()&&app::environment(L"XPFSR24_FG").empty()&&app::environment(L"XPFSR24_MODE")==L"observe","Native session retained renderer requests");
    {auto withNative=p;withNative.motionSource=1;xpfsr::nr::ControlChannel motionChannel;app::prepareSessionProfile(root,withNative,motionChannel);
     check(app::environment(L"XPFSR24_MOTION")==L"native-renderer","Native motion choice not passed to the renderer");
     app::prepareSessionProfile(root,withNative,motionChannel,true);check(app::environment(L"XPFSR24_MOTION").empty(),"Native comparison session kept a motion request");}
    {check(app::environment(L"XPFSR24_CPU_SCHEDULING").empty(),"CPU scheduling helper must be Off by default");
     auto helper=p;helper.cpuScheduling=1;xpfsr::nr::ControlChannel helperChannel;app::prepareSessionProfile(root,helper,helperChannel);
     check(app::environment(L"XPFSR24_CPU_SCHEDULING")==L"1","CPU scheduling choice not passed to the renderer");
     app::prepareSessionProfile(root,helper,helperChannel,true);check(app::environment(L"XPFSR24_CPU_SCHEDULING").empty(),"Native comparison session kept the CPU helper");}
    {check(app::environment(L"XPFSR24_FG_GPU").empty(),"Second-GPU FG must be off by default");
     auto two=p;two.fgGpu=1;xpfsr::nr::ControlChannel twoChannel;app::prepareSessionProfile(root,two,twoChannel);
     check(app::environment(L"XPFSR24_FG_GPU")==(app::effectiveFrameGeneration(two.frameGeneration)?std::wstring(L"second"):std::wstring()),"Second-GPU FG not passed to an FG session");
     app::prepareSessionProfile(root,two,twoChannel,true);check(app::environment(L"XPFSR24_FG_GPU").empty(),"Native comparison session kept second-GPU FG");}
    {check(app::environment(L"XPFSR24_LOD_BIAS").empty(),"Texture detail must be X-Plane's own by default");
     auto bias=p;bias.textureBias=2;xpfsr::nr::ControlChannel biasChannel;app::prepareSessionProfile(root,bias,biasChannel);
     check(app::environment(L"XPFSR24_LOD_BIAS")==(bias.mode?std::wstring(L"2"):std::wstring()),"Texture detail not passed to an FSR session");
     app::prepareSessionProfile(root,bias,biasChannel,true);check(app::environment(L"XPFSR24_LOD_BIAS").empty(),"Native comparison session kept the texture bias");}
    {check(app::environment(L"XPFSR24_ANTI_LAG").empty(),"Anti-Lag 2 must be Off by default");
     auto lag=p;lag.antiLag=1;xpfsr::nr::ControlChannel lagChannel;app::prepareSessionProfile(root,lag,lagChannel);
     check(app::environment(L"XPFSR24_ANTI_LAG")==(lag.mode?std::wstring(L"1"):std::wstring()),"Anti-Lag 2 choice not passed to an FSR session");
     app::prepareSessionProfile(root,lag,lagChannel,true);check(app::environment(L"XPFSR24_ANTI_LAG").empty(),"Native comparison session kept Anti-Lag 2");}
    auto registry=L"Software\\XPFSR24\\Tests\\Activation-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    struct Cleanup{std::wstring key;~Cleanup(){RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());}}cleanup{registry};
    const auto layers=registry+L"\\Layers",owner=registry+L"\\Owner";
    app::LayerRegistration registration(HKEY_CURRENT_USER,layers,owner);
    for(auto dir:{root,root/L"second"}){std::filesystem::create_directories(dir/L"auto");std::filesystem::create_directories(dir/L"layer");graphics::write(dir/L"auto/XPFSR24Implicit.json","fixture");graphics::write(dir/L"layer/XPFSR24Layer.dll","fixture");launcher::save(dir/L"settings.ini",p);}
    HKEY key{};check(RegCreateKeyExW(HKEY_CURRENT_USER,layers.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"Fixture registry setup");DWORD value=7;RegSetValueExW(key,L"Other-layer.json",0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value));RegCloseKey(key);
    registration.enable(root);DWORD enabled=99,bytes=sizeof(enabled);check(RegGetValueW(HKEY_CURRENT_USER,layers.c_str(),app::LayerRegistration::manifest(root).c_str(),RRF_RT_REG_DWORD,nullptr,&enabled,&bytes)==ERROR_SUCCESS&&enabled==0,"Implicit registration must use DWORD zero");
    const auto loaderKey=layers+L"\\LoaderSettings";const auto orderFile=app::loaderOrderFile(root);
    const auto orderText=graphics::read(orderFile);
    check(orderText.find("\"app_keys\":["+app::loaderJsonString(std::filesystem::weakly_canonical(root/L"sim/X-Plane.exe").wstring())+"]")!=std::string::npos&&orderText.find("unordered_layer_location")!=std::string::npos,"Loader order is not exact-application scoped or discarded other layers");
    check(app::loaderJsonString(L"C:\\Sim \"test\"\\\u98de.exe")=="\"C:\\\\Sim \\\"test\\\"\\\\\xe9\xa3\x9e.exe\"","Unicode or quote escaping changed the loader target");
    DWORD orderValue=1;bytes=sizeof(orderValue);check(RegGetValueW(HKEY_CURRENT_USER,loaderKey.c_str(),orderFile.c_str(),RRF_RT_REG_DWORD,nullptr,&orderValue,&bytes)==ERROR_SUCCESS&&orderValue==0,"Application-specific loader order was not registered");
    graphics::write(orderFile,orderText+" ");rejects([&]{registration.repair(root);});
    check(graphics::read(orderFile)==orderText+" ","Repair overwrote an edited loader profile");
    app::LoaderOrderRegistration orderRegistration(HKEY_CURRENT_USER,loaderKey,owner);orderRegistration.disable(root);
    check(graphics::read(orderFile)==orderText+" ","Removal deleted an edited loader profile");
    graphics::write(orderFile,orderText);registration.repair(root);
    const auto foreignOrder=root/L"other-loader/vk_loader_settings.json";std::filesystem::create_directories(foreignOrder.parent_path());graphics::write(foreignOrder,"unrelated loader fixture");
    check(RegOpenKeyExW(HKEY_CURRENT_USER,loaderKey.c_str(),0,KEY_SET_VALUE,&key)==ERROR_SUCCESS,"Loader conflict fixture open");DWORD zero=0;check(RegSetValueExW(key,foreignOrder.c_str(),0,REG_DWORD,reinterpret_cast<BYTE*>(&zero),sizeof(zero))==ERROR_SUCCESS,"Loader conflict fixture write");RegCloseKey(key);
    rejects([&]{registration.repair(root);});check(graphics::read(foreignOrder)=="unrelated loader fixture"&&graphics::read(orderFile)==orderText,"Conflicting loader profile was changed");
    check(RegOpenKeyExW(HKEY_CURRENT_USER,loaderKey.c_str(),0,KEY_SET_VALUE,&key)==ERROR_SUCCESS,"Loader conflict fixture cleanup");RegDeleteValueW(key,foreignOrder.c_str());RegCloseKey(key);registration.repair(root);
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
    launcher::writeSetting(L"InSim",L"Enabled",L"0",(root/L"settings.ini").c_str());launcher::writeSetting(L"InSim",L"Enabled",L"1",(root/L"second/settings.ini").c_str());
    auto foreign=p;foreign.simulator=(root/L"foreign-sim").wstring();std::filesystem::create_directories(root/L"foreign-sim");graphics::write(root/L"foreign-sim/X-Plane.exe","fixture");launcher::save(root/L"settings.ini",foreign);
    rejects([&]{registration.repair(root/L"second");});check(!_wcsicmp(app::registryString(HKEY_CURRENT_USER,owner.c_str(),L"Manifest").c_str(),app::LayerRegistration::manifest(root).c_str()),"Repair adopted a disabled owner for another simulator");launcher::save(root/L"settings.ini",p);
    registration.repair(root/L"second");check(!_wcsicmp(app::registryString(HKEY_CURRENT_USER,owner.c_str(),L"Manifest").c_str(),app::LayerRegistration::manifest(root/L"second").c_str())&&launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0","Repair did not replace the stale disabled same-simulator owner safely");
    bytes=sizeof(enabled);check(RegGetValueW(HKEY_CURRENT_USER,layers.c_str(),app::LayerRegistration::manifest(root).c_str(),RRF_RT_REG_DWORD,nullptr,&enabled,&bytes)==ERROR_FILE_NOT_FOUND,"Repair kept the stale implicit layer registered");
    registration.disable(root/L"second");registration.enable(root);
    // A deliberate setup in a different folder may replace our verified
    // previous plugin, including a build absent from a global digest list.
    app::installPlugin(root,root/L"sim");app::writeInstallationLink(root,root/L"sim");
    std::filesystem::create_directories(root/L"second/plugin/64");graphics::write(root/L"second/plugin/64/win.xpl","new installation plugin");
    const auto oldLinked=graphics::read(app::installationLinkFile(root/L"sim")),oldPlugin=app::fileHash(installedPlugin);
    graphics::write(app::installationLinkFile(root/L"sim"),"unknown link kept");rejects([&]{registration.preflight(root/L"second",true);});
    check(app::fileHash(installedPlugin)==oldPlugin&&launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"1","Preflight failure altered the existing installation");
    graphics::write(app::installationLinkFile(root/L"sim"),oldLinked);
    graphics::write(installedPlugin,"unrelated edited plugin");rejects([&]{registration.preflight(root/L"second",true);});rejects([&]{app::installPlugin(root/L"second",root/L"sim");});
    graphics::write(installedPlugin,"updated fixture plugin");check(app::fileHash(installedPlugin)==oldPlugin,"Fixture plugin restore mismatch");
    registration.preflight(root/L"second",true);app::installPlugin(root/L"second",root/L"sim");registration.enable(root/L"second",false,true);app::writeInstallationLink(root/L"second",root/L"sim");
    check(launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0"&&launcher::readSetting(root/L"second/settings.ini",L"InSim",L"Enabled")==L"1"&&app::linkedSettings(root/L"sim/X-Plane.exe")==std::filesystem::weakly_canonical(root/L"second/settings.ini"),"Verified migration left two active owners or a stale setup link");
    rejects([&]{registration.repair(root);}); // Repair alone must never move an enabled owner.
    registration.preflight(root,true);app::installPlugin(root,root/L"sim");registration.enable(root,false,true);app::writeInstallationLink(root,root/L"sim");
    check(app::fileHash(installedPlugin)==oldPlugin&&launcher::readSetting(root/L"second/settings.ini",L"InSim",L"Enabled")==L"0","Verified reverse migration did not restore single ownership");
    std::filesystem::remove(installedPlugin);check(registration.preflight(root/L"second",true)==root,"Owned link could not recover its removed plugin");app::installPlugin(root,root/L"sim");
    app::removeInstallationLink(root,root/L"sim");
    auto previous=registration.preflight(root/L"second",true);check(previous==root,"Legacy registered owner was not recovered without a link");
    app::installPlugin(root/L"second",root/L"sim",previous);registration.enable(root/L"second",false,true);
    std::filesystem::remove(installedPlugin); // Old plugin already removed during a failed reinstall.
    previous=registration.preflight(root,true);check(previous==root/L"second","Missing old plugin blocked verified registration recovery");
    app::installPlugin(root,root/L"sim",previous);registration.enable(root,false,true);app::writeInstallationLink(root,root/L"sim");
    check(app::fileHash(installedPlugin)==oldPlugin&&launcher::readSetting(root/L"second/settings.ini",L"InSim",L"Enabled")==L"0","Legacy/missing-plugin migration left the wrong active owner");
    // Removal works without a connected simulator and leaves unrelated layers.
    std::filesystem::remove(root/L"sim/X-Plane.exe");registration.disable(root);bytes=sizeof(value);check(RegGetValueW(HKEY_CURRENT_USER,layers.c_str(),L"Other-layer.json",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS&&value==7,"Unrelated Vulkan layer changed");
    check(app::registryString(HKEY_CURRENT_USER,owner.c_str(),L"Manifest").empty()&&launcher::readSetting(root/L"settings.ini",L"InSim",L"Enabled")==L"0","Disable left activation owned/enabled");
    check(!std::filesystem::exists(orderFile)&&graphics::read(foreignOrder)=="unrelated loader fixture","Uninstall retained the owned loader profile or removed unrelated data");
    std::cout<<"PASS: exact selected executable, opt-in/disable, inherited-profile rejection, fail-closed logs, shared provider/session mapping, registry ownership and disconnected-drive cleanup.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
