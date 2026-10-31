// SPDX-License-Identifier: MIT
#include "in_sim_panel.hpp"
#include "panel_presence.hpp"
#include <iostream>
#include <set>
static void check(bool value,const char*why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Fresh fixture required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Fixture exists");std::filesystem::create_directories(root);
    auto file=root/L"settings.ini";launcher::Preferences initial;initial.mode=2;launcher::save(file,initial);
    std::wstring name=L"Local\\XPFSRPanelTest-"+std::to_wstring(GetCurrentProcessId());xpfsr::nr::ControlChannel writer,reader;
    check(writer.create(name.c_str(),initial.nr)&&reader.open(name.c_str()),"NR IPC attach");int publications=0;
    insim::Panel panel;panel.bind(file,&writer,true,false,[&](const auto&){++publications;});
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
        panel.act({insim::Kind::Help,xpfsr::nr::Order});auto help=panel.view(size.first,size.second);for(const auto&i:help)check(!i.button||i.action.kind!=insim::Kind::NRField,"Help overlay left editable background controls");
        panel.act({insim::Kind::Help,-1});panel.act({insim::Kind::Tab,0});for(int y=0;y<=panel.maxScroll(size.second);++y){panel.scroll=y;for(const auto&i:panel.view(size.first,size.second))check(i.rect.y<132||i.rect.y+i.rect.h<=size.second-120||!i.button,"Flight control overlaps footer");}
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
    panel.act({insim::Kind::Help,203});check(panel.help().find("never")!=std::string::npos||panel.help().find("without loading")!=std::string::npos,"Missing model import guidance");panel.act({insim::Kind::Help,-1});
    panel.nrStatus="NR bypassed: NR model missing";panel.act({insim::Kind::Budget,2});check(panel.selectedBudget()==2&&panel.notice.find("Waiting for a model")!=std::string::npos,"Budget selection falsely implied missing NR was active");
    before=publications;panel.act({insim::Kind::Budget,2});check(publications==before&&panel.notice.find("Selected: Quality 67%")!=std::string::npos,"Selecting the current budget gave no feedback or restarted NR");
    panel.act({insim::Kind::Tab,0});panel.scroll=panel.maxScroll(740);int highlights=0;
    for(const auto&item:panel.view(880,740))if(item.action.kind==insim::Kind::Budget&&item.selected){++highlights;check(item.action.field==2&&item.text.find("Selected")!=std::string::npos,"Wrong budget highlighted");}
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
    status.recordings=101;panel.observe(status,1016);check(panel.activity.find("FSR ACTIVE")!=std::string::npos,"Advancing renderer not confirmed");panel.observe(status,5000);check(panel.activity.find("waiting")!=std::string::npos,"Stale activity stayed green");
    app::PanelPresenceChannel owner,observer;check(owner.create(GetCurrentProcessId()),"Presence owner");check(!observer.visible(GetCurrentProcessId(),1000),"Hidden panel paused FG");owner.setVisible(true);check(observer.visible(GetCurrentProcessId(),1010),"Visible panel was not observed");owner.close();check(!observer.visible(GetCurrentProcessId(),1020),"Plugin shutdown left FG paused");
    std::cout<<"PASS: live NR/processing persistence, stale desktop edits, opt-in/no-op safety, viewport/scroll/help coverage, activity proof and panel-presence lifetime.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
