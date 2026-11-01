// SPDX-License-Identifier: MIT
#include "in_sim_panel.hpp"
#include "panel_presence.hpp"
#include "in_sim_binding.hpp"
#include <iostream>
#include <set>
static void check(bool value,const char*why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Fresh fixture required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Fixture exists");std::filesystem::create_directories(root);
    auto file=root/L"settings.ini";launcher::Preferences initial;initial.mode=2;launcher::save(file,initial);
    insim::Panel selector;
    check(selector.choiceOptions({insim::Kind::Renderer}).size()==3,"Renderer dropdown still offers Automatic");
    insim::Panel disconnected;
    check(disconnected.maxScroll(480)==0,"Unlinked setup displayed an empty scrolling viewport");
    for(auto size:{std::pair{640,480},std::pair{880,740}}){for(const auto &item:disconnected.view(size.first,size.second)){
        check(!item.button||item.action.kind==insim::Kind::Close||item.action.kind==insim::Kind::Tab||item.action.kind==insim::Kind::Changelog,"Unlinked panel displayed clickable rendering settings");
        if(item.widget==insim::Widget::Label&&item.rect.h>20&&item.rect.y<size.second-insim::footerHeight)check(item.rect.y>=insim::contentTop&&item.rect.y+item.rect.h<=size.second-insim::footerHeight,"Connection guidance overlaps the footer");}}
    auto sim=root/L"selected-sim";std::filesystem::create_directories(sim);graphics::write(sim/L"X-Plane.exe","fixture");
    auto installed=root/L"installed";std::filesystem::create_directories(installed/L"auto");auto installedFile=installed/L"settings.ini",manifest=installed/L"auto/XPFSR24Implicit.json";
    auto installChoices=initial;installChoices.simulator=sim.wstring();launcher::save(installedFile,installChoices);launcher::writeSetting(L"InSim",L"Enabled",L"1",installedFile.c_str());graphics::write(manifest,"fixture");
    auto recovered=insim::resolveSettings(sim/L"X-Plane.exe",{},{},manifest);check(recovered.file==std::filesystem::weakly_canonical(installedFile)&&recovered.recovered,"Missing startup environment lost the installed profile");
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},{}).reason.find("No installation registration")!=std::string::npos,"Missing registration lost its diagnostic reason");
    check(insim::resolveSettings(sim/L"other.exe",{},{},manifest).file.empty(),"Settings recovery accepted a different executable");
    check(insim::resolveSettings(sim/L"other.exe",{},{},manifest).reason.find("does not match")!=std::string::npos,"Simulator mismatch lost its diagnostic reason");
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},installed/L"runtime",{}).file==std::filesystem::weakly_canonical(installedFile),"Legacy desktop runtime did not recover its own settings");
    launcher::writeSetting(L"InSim",L"Enabled",L"0",installedFile.c_str());check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},manifest).file.empty(),"Disabled installation was recovered through registration");
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},manifest).reason.find("disabled")!=std::string::npos,"Disabled integration lost its diagnostic reason");
    launcher::writeSetting(L"InSim",L"Enabled",L"1",installedFile.c_str());
    auto missingManifest=installed/L"gone/auto/XPFSR24Implicit.json";
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},missingManifest).reason.find("manifest is missing")!=std::string::npos,"Missing manifest lost its diagnostic reason");
    std::filesystem::create_directories(installed/L"plugin/64");std::filesystem::create_directories(sim/L"Resources/plugins/XPFSR24/64");
    graphics::write(installed/L"plugin/64/win.xpl","owned plugin fixture");graphics::write(sim/L"Resources/plugins/XPFSR24/64/win.xpl","owned plugin fixture");
    auto pluginHash=app::fileHash(installed/L"plugin/64/win.xpl");std::wstring pluginWide(pluginHash.begin(),pluginHash.end());
    launcher::writeSetting(L"InSim",L"PluginSHA256",pluginWide.c_str(),installedFile.c_str());
    app::writeInstallationLink(installed,sim);
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},{}).file==std::filesystem::weakly_canonical(installedFile),"Missing registry did not recover the verified installed link");
    const auto staleRoot=root/L"stale",staleManifest=staleRoot/L"auto/XPFSR24Implicit.json",staleFile=staleRoot/L"settings.ini";
    std::filesystem::create_directories(staleManifest.parent_path());graphics::write(staleManifest,"fixture");launcher::save(staleFile,installChoices);launcher::writeSetting(L"InSim",L"Enabled",L"0",staleFile.c_str());
    check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},staleManifest).file==std::filesystem::weakly_canonical(installedFile),"Stale disabled registration hid the installed profile");
    check(insim::resolveSettings(sim/L"X-Plane.exe",staleFile,{},manifest).file==std::filesystem::weakly_canonical(staleFile),"Recovery redirected an existing explicit session");
    launcher::writeSetting(L"InSim",L"Enabled",L"0",installedFile.c_str());check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},{}).file.empty(),"Installed link re-enabled an opted-out profile");launcher::writeSetting(L"InSim",L"Enabled",L"1",installedFile.c_str());
    graphics::write(sim/L"Resources/plugins/XPFSR24/64/win.xpl","changed plugin");check(insim::resolveSettings(sim/L"X-Plane.exe",{},{},{}).file.empty(),"Setup recovery accepted a different installed plugin");graphics::write(sim/L"Resources/plugins/XPFSR24/64/win.xpl","owned plugin fixture");
    const auto link=app::installationLinkFile(sim);graphics::write(link,"keep this unknown link");bool unknownKept=false;try{app::writeInstallationLink(installed,sim);}catch(...){unknownKept=true;}app::removeInstallationLink(installed,sim);check(unknownKept&&graphics::read(link)=="keep this unknown link","Unknown setup link was overwritten or removed");
    graphics::write(link,app::installationLinkContents(installed,pluginHash));app::removeInstallationLink(installed,sim);check(!std::filesystem::exists(link),"Owned setup link was not removed");
    std::filesystem::create_directories(installed/L"logs");graphics::write(installed/L"logs/renderer.log","old renderer sentinel");
    insim::ConfigurationSession only;auto onlyToken=only.open(installed);auto fresh=graphics::read(installed/L"logs/renderer.log");check(graphics::read(installed/L"logs/renderer.log.previous")=="old renderer sentinel"&&fresh.find("plugin-only")!=std::string::npos&&fresh.find(onlyToken)!=std::string::npos,"Plugin-only session retained stale renderer logs");
    insim::ConfigurationSession second;check(second.open(installed)!=onlyToken&&graphics::read(installed/L"logs/renderer.log")==fresh&&only.open(installed)==onlyToken,"A second configuration session or repeated poll erased active diagnostics");
    disconnected.bind(installedFile,nullptr,false,false);disconnected.configurationOnly("Renderer absent; saved settings recovered");app::Telemetry absent{};absent.active=1;absent.recordings=100;disconnected.observe(absent,1000);++absent.recordings;disconnected.observe(absent,1100);check(disconnected.activity.find("NOT LOADED")!=std::string::npos&&!disconnected.rendererLinked(),"Recovered settings falsely confirmed rendering");
    const auto beforeRenderer=launcher::serialized(launcher::load(installedFile));
    check(disconnected.choiceOptions({insim::Kind::Renderer}).size()==3,"Unexpected Automatic renderer control");
    check(disconnected.choiceCurrent({insim::Kind::Renderer})==1&&xp_renderer::read(sim)==xp_renderer::Choice::automatic,"Opening the panel changed the legacy choice");
    disconnected.act({insim::Kind::OpenChoice,int(insim::Kind::Renderer),0});disconnected.act({insim::Kind::ChooseOption,1});
    check(xp_renderer::read(sim)==xp_renderer::Choice::fsr&&disconnected.notice.find("restart")!=std::string::npos,"Renderer choice was not saved with restart feedback");
    check(launcher::serialized(launcher::load(installedFile))==beforeRenderer,"Renderer switch reset FSR, NR, FG or graphics choices");
    insim::Panel reopenedRenderer;reopenedRenderer.bind(installedFile,nullptr,false,false);
    check(reopenedRenderer.choiceCurrent({insim::Kind::Renderer})==1,"Renderer dropdown did not restore selection");
    int repairs=0;disconnected.repairSupport([&]{++repairs;});
    for(auto size:{std::pair{640,480},std::pair{880,740}}){disconnected.act({insim::Kind::Tab,0});bool repairButton=false;for(const auto& item:disconnected.view(size.first,size.second))repairButton|=item.button&&item.action.kind==insim::Kind::RepairStartup;check(repairButton,"Repair startup is hidden in a supported viewport");}
    disconnected.act({insim::Kind::RepairStartup});check(repairs==1&&launcher::serialized(launcher::load(installedFile))==beforeRenderer&&disconnected.notice.find("Repairing")!=std::string::npos,"Repair has no click feedback or changed saved choices");disconnected.repairSupport({});
    insim::ConfigurationSession aaSession;auto aaToken=aaSession.open(root/L"aa-session","Native AA owns this session. FSR intentionally inactive.");
    check(graphics::read(root/L"aa-session/logs/renderer.log").find("intentionally inactive")!=std::string::npos&&graphics::read(root/L"aa-session/logs/renderer.log").find("Restart is required")==std::string::npos&&!aaToken.empty(),"Intentional renderer selection was logged as a startup fault");
    disconnected.configurationOnly("Startup bypass: actual activation failure",true);disconnected.observe(absent,1200);
    check(disconnected.activity.find("BYPASSED")!=std::string::npos&&disconnected.notice.find("actual activation failure")!=std::string::npos&&!disconnected.rendererLinked(),"Startup bypass was mislabeled as a missing layer");
    for(auto size:{std::pair{640,480},std::pair{880,740},std::pair{1280,900}}){
        auto notice=insim::footerNotice(size.first,size.second),guidance=insim::footerGuidance(size.first,size.second);
        check(notice.y>=size.second-insim::footerHeight&&notice.y+notice.h+4<=guidance.y&&guidance.y+guidance.h<=size.second-6,"Wrapped footer messages overlap or escape the panel");
        bool sawNotice=false,sawGuidance=false;
        for(auto &item:disconnected.view(size.first,size.second)){sawNotice|=item.rect.y==notice.y&&item.rect.h==notice.h&&item.text==disconnected.notice;sawGuidance|=item.rect.y==guidance.y&&item.rect.h==guidance.h;}
        check(sawNotice&&sawGuidance,"Footer drawing bypassed the bounded wrapped layout");
    }
    std::wstring name=L"Local\\XPFSRPanelTest-"+std::to_wstring(GetCurrentProcessId());xpfsr::nr::ControlChannel writer,reader;
    check(writer.create(name.c_str(),initial.nr)&&reader.open(name.c_str()),"NR IPC attach");int publications=0;
    insim::Panel panel;panel.bind(file,&writer,true,false,[&](const auto&){++publications;});
    int logOpens=0;panel.logSupport([&]{++logOpens;});
    int reportOpens=0;panel.reportSupport([&]{++reportOpens;});
    for(auto size:{std::pair{640,480},std::pair{880,740}}){panel.act({insim::Kind::Tab,2});bool openLog=false;
        for(const auto& item:panel.view(size.first,size.second))openLog|=item.button&&item.action.kind==insim::Kind::Logs;
        check(openLog,"Log folder button is hidden in a supported viewport");}
    panel.act({insim::Kind::Logs});check(logOpens==1&&panel.notice.find("FSR-Logs")!=std::string::npos,"Log button has no action or location feedback");panel.act({insim::Kind::Tab,0});
    const auto reportBefore=launcher::serialized(launcher::load(file));panel.act({insim::Kind::Report});check(reportOpens==1&&panel.notice.find("Off until")!=std::string::npos&&launcher::serialized(launcher::load(file))==reportBefore,"Report preview enabled sharing or changed image settings");
    auto staleDesktop=launcher::load(file);
    for(auto field:{xpfsr::nr::Enabled,xpfsr::nr::Order,xpfsr::nr::Scope})panel.act({insim::Kind::NRField,field,1});
    xpfsr::nr::Controls live;check(reader.read(live)&&live[xpfsr::nr::Enabled]&&live[xpfsr::nr::Order]&&live[xpfsr::nr::Scope],"Live NR changes did not reach renderer IPC");
    auto desktop=staleDesktop;desktop.enabled=false;
    launcher::transactChoices(file,staleDesktop,desktop,[&](const auto&p){writer.write(p.nr);});panel.refresh();
    check(!panel.choices.enabled&&panel.choices.nr[xpfsr::nr::Scope]==1,"Desktop/panel edits conflict");
    panel.act({insim::Kind::Processing});check(launcher::load(file).enabled,"Live processing choice did not save");
    insim::Panel reopened;reopened.bind(file,&writer,true,false);check(reopened.choices.nr[xpfsr::nr::Order]==1&&reopened.choices.neuralRendering!=0,"Reopened panel lost choices");
    auto saved=launcher::serialized(launcher::load(file));bool refused=false;try{panel.act({insim::Kind::FrameGen,0,1});}catch(...){refused=true;}
    check(refused&&launcher::serialized(launcher::load(file))==saved,"NR + FG opt-in bypassed or failed change saved");
    panel.act({insim::Kind::NRField,xpfsr::nr::AllowFG,1});panel.act({insim::Kind::FrameGen,0,1});
    check(panel.choices.frameGeneration==1&&!panel.choices.cockpitRestore,"FG selection did not retain compositor compatibility");
    int before=publications;panel.act({insim::Kind::NRField,xpfsr::nr::AllowFG,1});check(publications==before,"Clamped no-op published/reset NR again");
    insim::Panel unconnected;unconnected.bind(file,nullptr,false,false);unconnected.act({insim::Kind::NRField,xpfsr::nr::Intensity,1});check(unconnected.notice.find("next session")!=std::string::npos,"Unconnected NR was advertised live instead of queued");
    for(auto size:{std::pair{640,480},std::pair{880,740},std::pair{1280,900}}){
        std::set<int> reached;panel.act({insim::Kind::Tab,1});
        for(int y=0;y<=panel.maxScroll(size.second)+46;y+=23){panel.scroll=y;auto items=panel.view(size.first,size.second);
            for(const auto&i:items){check(i.rect.x>=0&&i.rect.y>=0&&i.rect.x+i.rect.w<=size.first&&i.rect.y+i.rect.h<=size.second,"Panel content escaped viewport");if(i.button&&i.action.kind==insim::Kind::NRField)reached.insert(i.action.field);if(i.action.kind==insim::Kind::OpenChoice&&i.action.field==int(insim::Kind::NRField))reached.insert(i.action.delta);}}
        check(reached.size()==xpfsr::nr::Count,"NR field unreachable by scrolling");
        panel.scroll=panel.maxScroll(size.second);check(!panel.wheel(10000,size.second),"Bottom overscroll changed layout");panel.scroll=0;check(!panel.wheel(-10000,size.second),"Top overscroll changed layout");
        // v2: "?" pins a hover card and keeps the user on the same page with live controls.
        const int tabBefore=panel.tab;const auto unpinned=panel.view(size.first,size.second).size();
        panel.act({insim::Kind::Help,xpfsr::nr::Order});check(panel.pinnedHelp==xpfsr::nr::Order&&panel.helpField<0&&panel.tab==tabBefore,"Help click left the current page");
        check(panel.view(size.first,size.second).size()==unpinned,"Pinned help replaced the settings it explains");
        check(!panel.helpText(xpfsr::nr::Order).empty()&&!panel.helpTitle(xpfsr::nr::Order).empty(),"Missing hover card text");
        panel.act({insim::Kind::Help,xpfsr::nr::Order});check(panel.pinnedHelp<0,"Second help click did not unpin the card");
        panel.act({insim::Kind::Help,-1});panel.act({insim::Kind::Tab,0});check(panel.pinnedHelp<0,"Tab change kept a pinned card");for(int y=0;y<=panel.maxScroll(size.second);++y){panel.scroll=y;for(const auto&i:panel.view(size.first,size.second))check(i.rect.y<insim::contentTop||i.rect.y+i.rect.h<=size.second-insim::footerHeight||!i.button,"Flight control overlaps footer");}
    }
    panel.act({insim::Kind::Tab,1});panel.scroll=0;
    auto widgets=panel.view(880,740);bool toggle=false,slider=false,close=false;
    for(auto &item:widgets){toggle|=item.action.kind==insim::Kind::NRField&&item.action.field==xpfsr::nr::Enabled&&item.widget==insim::Widget::Toggle;slider|=item.action.kind==insim::Kind::NRField&&item.action.field==xpfsr::nr::Intensity&&item.widget==insim::Widget::Slider;close|=item.action.kind==insim::Kind::Close;}
    check(toggle&&slider&&close,"Expected native in-sim toggle, numeric slider and accessible close control");
    for(auto field:{xpfsr::nr::Intensity,xpfsr::nr::Tone,xpfsr::nr::Structure,xpfsr::nr::Skin,xpfsr::nr::Colour,xpfsr::nr::Detail,xpfsr::nr::Highlight,xpfsr::nr::History,xpfsr::nr::Scale,xpfsr::nr::Passes}){
        panel.slider(field,-1000);check(panel.choices.nr[field]==xpfsr::nr::settings[field].minimum,"Slider lower limit failed");panel.slider(field,10000);check(panel.choices.nr[field]==xpfsr::nr::settings[field].maximum,"Slider upper limit failed");
    }
    panel.slider(xpfsr::nr::Intensity,37);panel.slider(xpfsr::nr::Scale,64);check(panel.choices.nr[xpfsr::nr::Intensity]==37&&panel.choices.nr[xpfsr::nr::Scale]==67,"Sliders lost precision or invalid discrete model scale");
    panel.act({insim::Kind::OpenChoice,int(insim::Kind::NRField),xpfsr::nr::Style});panel.act({insim::Kind::ChooseOption,2});check(panel.choices.nr[xpfsr::nr::Style]==2&&!panel.dropdownOpen,"Dropdown selection did not save");
    for(auto size:{std::pair{640,480},std::pair{880,740}}){panel.act({insim::Kind::OpenChoice,int(insim::Kind::Mode),0});std::set<int> options;for(int position=0;position<=panel.maxDropdownScroll(size.second)+36;++position){panel.dropdownScroll=position;for(auto &item:panel.view(size.first,size.second))if(item.action.kind==insim::Kind::ChooseOption)options.insert(item.action.field);}check(options.size()==5,"A dropdown option cannot be reached at a supported window size");panel.dropdownScroll=panel.maxDropdownScroll(size.second);check(!panel.wheel(10000,size.second),"Dropdown bottom overscroll changed its state");panel.act({insim::Kind::CloseChoice});}
    panel.act({insim::Kind::OpenChoice,int(insim::Kind::Mode),0});auto edit=launcher::load(file),remote=edit;remote.mode=1;launcher::transactChoices(file,edit,remote);panel.act({insim::Kind::ChooseOption,4});check(panel.choices.mode==4,"Concurrent dropdown edit applied a relative stale selection");
    panel.act({insim::Kind::Graphics,0,1});check(panel.choices.mode==4&&panel.choices.frameGeneration==1&&panel.choices.manualPending,"Custom MSAA reset FSR/FG or failed to mark pending graphics");
    panel.act({insim::Kind::Tab,3});panel.scroll=panel.maxScroll(740);widgets=panel.view(880,740);bool apply=false;for(auto &item:widgets)apply|=item.action.kind==insim::Kind::ApplyGraphics;check(apply,"Graphics apply-after-exit action missing");
    int queued=0;panel.graphicsSupport([&]{++queued;});panel.act({insim::Kind::ApplyGraphics});check(queued==1,"Deferred graphics callback not invoked");
    panel.act({insim::Kind::FGToggle,0,-1});check(!panel.choices.frameGeneration&&panel.choices.lastFrameGeneration==1,"FG Off lost the selected provider");
    panel.act({insim::Kind::FGProvider,0,1});check(!panel.choices.frameGeneration&&panel.choices.lastFrameGeneration==2,"Choosing a provider enabled FG while Off");
    insim::Panel fgReopened;fgReopened.bind(file,&writer,true,false);check(fgReopened.choices.lastFrameGeneration==2,"Reopening lost the last FG provider");fgReopened.act({insim::Kind::FGToggle,0,1});check(fgReopened.choices.frameGeneration==2,"FG On did not restore the remembered provider");
    panel.act({insim::Kind::Tab,1});widgets=panel.view(640,480);bool importVisible=false;int firstTabY=0;
    for(const auto &item:widgets){importVisible|=item.action.kind==insim::Kind::ImportModel; if(item.action.kind==insim::Kind::Tab)firstTabY=item.rect.y;}
    check(importVisible&&firstTabY>=108,"Model import is hidden or status/tabs spacing regressed");
    saved=launcher::serialized(launcher::load(file));refused=false;try{panel.act({insim::Kind::ImportModel});}catch(...){refused=true;}
    check(refused&&launcher::serialized(launcher::load(file))==saved,"Unavailable import changed preferences");
    int imports=0;panel.modelSupport([&]{++imports;panel.modelImportBusy=true;});panel.act({insim::Kind::ImportModel});panel.act({insim::Kind::ImportModel});
    check(imports==1&&launcher::serialized(launcher::load(file))==saved,"Duplicate import or model picker changed rendering choices");
    {const auto guide=panel.helpText(203);check(guide.find("never")!=std::string::npos||guide.find("without running")!=std::string::npos,"Missing model import guidance");}
    panel.nrStatus="NR bypassed: NR model missing";panel.act({insim::Kind::Budget,2});check(panel.selectedBudget()==2&&panel.notice.find("Waiting for a model")!=std::string::npos,"Budget selection falsely implied missing NR was active");
    before=publications;panel.act({insim::Kind::Budget,2});check(publications==before&&panel.notice.find("Selected: Quality 67%")!=std::string::npos,"Selecting the current budget gave no feedback or restarted NR");
    panel.act({insim::Kind::Tab,0});panel.scroll=panel.maxScroll(740);int highlights=0;
    for(const auto&item:panel.view(880,740))if(item.action.kind==insim::Kind::Budget&&item.selected){++highlights;check(item.action.field==2&&item.text.find("Quality 67%")!=std::string::npos,"Wrong budget highlighted");}
    check(highlights==1,"Selected budget lacks a visible cue");
    panel.act({insim::Kind::NRField,xpfsr::nr::Enabled,-1});panel.nrStatus="NR Off";panel.act({insim::Kind::Budget,0});check(panel.notice.find("NR is Off")!=std::string::npos,"Budget feedback ignored NR Off");
    auto beforeCloud=launcher::load(file);panel.act({insim::Kind::CloudPreset,0,1});
    check(panel.choices.mode==4&&panel.choices.manual.scale==1&&panel.choices.manual.cloudQuality==3&&panel.choices.manualPending,"Ultra Quality cloud setup did not queue its settings");
    check(panel.choices.frameGeneration==beforeCloud.frameGeneration&&panel.choices.neuralRendering==beforeCloud.neuralRendering&&panel.choices.manual.msaa==beforeCloud.manual.msaa&&panel.choices.manual.outputWidth==beforeCloud.manual.outputWidth,"Cloud setup reset unrelated choices");
    panel.act({insim::Kind::CloudPreset,0,2});check(panel.choices.mode==3&&panel.choices.manual.scale==0&&panel.choices.manual.cloudQuality==3,"Native AA cloud setup selected the wrong provider or input");
    insim::Panel cloudReopened;cloudReopened.bind(file,&writer,true,false);check(cloudReopened.choices.manual.cloudQuality==3&&cloudReopened.choices.manualPending,"Cloud quality did not survive reopening");
    cloudReopened.act({insim::Kind::OpenChoice,int(insim::Kind::Graphics),8});cloudReopened.act({insim::Kind::ChooseOption,0});check(cloudReopened.choices.manual.cloudQuality==-1&&cloudReopened.choices.frameGeneration==beforeCloud.frameGeneration,"Keep cloud quality reset FG or failed to save");
    auto launchCloud=cloudReopened.choices;launchCloud.manualPending=false;launchCloud.manual.cloudQuality=3;check(launcher::graphicsForLaunch(launchCloud).cloudQuality==-1,"Ordinary launch forced cloud quality again");
    cloudReopened.act({insim::Kind::Mode,0,-2});cloudReopened.act({insim::Kind::CloudPreset,0,1});check(cloudReopened.choices.mode==2,"Cloud setup switched the FSR 3 provider family");
    cloudReopened.act({insim::Kind::CloudPreset,0,2});check(cloudReopened.choices.mode==1,"Cloud Native AA switched the FSR 3 provider family");
    saved=launcher::serialized(launcher::load(file));cloudReopened.act({insim::Kind::CloudPreset,0,0});check(launcher::serialized(launcher::load(file))==saved,"Keep cloud choices changed preferences");
    for(auto size:{std::pair{640,480},std::pair{880,740}}){cloudReopened.act({insim::Kind::Tab,3});bool cloud=false,clarity=false,applyCloud=false;
        for(int position=0;position<=cloudReopened.maxScroll(size.second)+46;position+=23){cloudReopened.scroll=position;for(auto &item:cloudReopened.view(size.first,size.second)){cloud|=item.action.kind==insim::Kind::OpenChoice&&item.action.field==int(insim::Kind::Graphics)&&item.action.delta==8;clarity|=item.action.kind==insim::Kind::OpenChoice&&item.action.field==int(insim::Kind::CloudPreset);applyCloud|=item.action.kind==insim::Kind::ApplyGraphics;}}
        check(cloud&&clarity&&applyCloud,"Cloud controls or Apply cannot be reached in a supported viewport");}
    app::Telemetry status{};status.active=1;status.recordings=100;panel.observe(status,1000);check(panel.activity.find("FSR ACTIVE")==std::string::npos,"Static telemetry falsely confirmed activity");
    status.recordings=101;panel.observe(status,1016);check(panel.activity.find("FSR ACTIVE")!=std::string::npos,"Advancing renderer not confirmed");panel.observe(status,5000);check(panel.activity.find("WAITING")!=std::string::npos,"Stale activity stayed green");
    insim::Panel fgLive;bool liveFG=false;
    fgLive.bind(file,&writer,true,true,[&](const auto&p){liveFG=p.frameGeneration!=0;});
    fgLive.act({insim::Kind::FGToggle,0,-1});
    fgLive.act({insim::Kind::FGToggle,0,1});check(liveFG&&fgLive.notice.find("Close this panel")!=std::string::npos,"FG On did not publish its live state or explain panel pause");
    check(fgLive.frameGenerationStatus().find("Waiting")!=std::string::npos,"Saved FG On falsely proved generation");
    app::Telemetry fgStatus{};fgStatus.active=1;fgStatus.fgActive=1;fgStatus.recordings=1;fgLive.observe(fgStatus,1000);fgStatus.recordings=2;fgLive.observe(fgStatus,1016);
    check(fgLive.frameGenerationStatus().find("Starting")!=std::string::npos,"FG active flag without generated/presented frames was called running");
    fgStatus.fgGenerated=8;fgStatus.displayedPresents=16;++fgStatus.recordings;fgLive.observe(fgStatus,1032);check(fgLive.frameGenerationStatus().find("Running")!=std::string::npos,"Confirmed FG counters did not report running");
    fgStatus.fgActive=0;strcpy(fgStatus.fgReason,"FG paused while in-sim setup is visible; selection retained");++fgStatus.recordings;fgLive.observe(fgStatus,1048);check(fgLive.frameGenerationStatus().find("Paused")!=std::string::npos,"Panel pause falsely reported running");
    strcpy(fgStatus.fgReason,"No three spare graphics queues for FG");++fgStatus.recordings;fgLive.observe(fgStatus,1064);check(fgLive.frameGenerationStatus().find("queues")!=std::string::npos,"FG startup error was hidden");
    fgStatus.fgActive=1;fgLive.observe(fgStatus,5000);check(fgLive.frameGenerationStatus().find("Waiting")!=std::string::npos,"Stale FG counters stayed running");
    fgLive.act({insim::Kind::FGToggle,0,-1});check(!liveFG&&fgLive.frameGenerationStatus().find("Off")==0&&fgLive.notice.find("live")!=std::string::npos,"FG Off was only saved or kept reporting active");
    fgReopened.act({insim::Kind::FGToggle,0,1});check(fgReopened.frameGenerationStatus().find("Restart needed")==0&&fgReopened.notice.find("not running yet")!=std::string::npos,"Session started without FG falsely advertised live enable");
    insim::Panel fgPanel;fgPanel.bind(file,&writer,true,true);
    app::Telemetry fgSample{};fgSample.active=1;fgSample.fgActive=1;fgSample.recordings=1;
    fgPanel.observe(fgSample,1000);++fgSample.recordings;fgPanel.observe(fgSample,1020);
    check(fgPanel.frameGenerationStatus().find("Starting")==0,"FG active flag alone implied completed frames");
    fgSample.fgGenerated=3;fgSample.displayedPresents=6;++fgSample.recordings;fgPanel.observe(fgSample,1040);
    check(fgPanel.frameGenerationStatus().find("Running")==0,"Confirmed current FG counters were hidden");
    fgSample.fgActive=0;strcpy(fgSample.fgReason,"FG paused while in-sim setup is visible");++fgSample.recordings;fgPanel.observe(fgSample,1060);
    check(fgPanel.frameGenerationStatus().find("Paused")==0,"An open panel with paused FG showed running");
    fgPanel.observe(fgSample,5000);check(fgPanel.frameGenerationStatus().find("Waiting")==0,"Stale frame telemetry stayed active");
    app::PanelPresenceChannel owner,observer;check(owner.create(GetCurrentProcessId()),"Presence owner");check(!observer.visible(GetCurrentProcessId(),1000),"Hidden panel paused FG");owner.setVisible(true);check(observer.visible(GetCurrentProcessId(),1010),"Visible panel was not observed");owner.close();check(!observer.visible(GetCurrentProcessId(),1020),"Plugin shutdown left FG paused");
    std::cout<<"PASS: live NR/processing persistence, stale desktop edits, opt-in/no-op safety, viewport/scroll/help coverage, activity proof and panel-presence lifetime.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
