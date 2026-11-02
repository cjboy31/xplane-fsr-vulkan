// SPDX-License-Identifier: MIT
#include "settings_transaction.hpp"
#include <iostream>
#include <thread>
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int wmain(int argc,wchar_t **argv){try{
    require(argc>=2,"Fresh fixture required");graphics::fs::path root=argv[1],file=root/L"settings.ini";
    if(argc==4){
        auto before=launcher::load(file);int writer=_wtoi(argv[2]);HANDLE start=OpenEventW(SYNCHRONIZE,FALSE,argv[3]);require(start!=nullptr,"Writer barrier");require(WaitForSingleObject(start,10000)==WAIT_OBJECT_0,"Writer start");CloseHandle(start);
        for(int i=0;i<50;++i){auto proposed=before;if(writer==0)proposed.nr[xpfsr::nr::Intensity]=20+i;else proposed.nr[xpfsr::nr::Scope]=i%2;
            launcher::transactChoices(file,before,proposed);before=proposed;}
        return 0;
    }
    require(!graphics::fs::exists(root),"Do not reuse a fixture");graphics::fs::create_directories(root);
    launcher::Preferences base;launcher::save(file,base);WritePrivateProfileStringW(L"Unrelated",L"Keep",L"yes",file.c_str());
    auto panel=base,desktop=base;panel.nr[xpfsr::nr::Intensity]=42;panel.fgPacing=1;panel.fgCap=1;panel.motionSource=1;
    launcher::transactChoices(file,base,panel);
    desktop.frameGeneration=3;launcher::Preferences published;
    launcher::transactChoices(file,base,desktop,[&](const auto&p){published=p;});
    require(published.nr[xpfsr::nr::Intensity]==42&&published.frameGeneration==3&&published.fgPacing==1&&published.fgCap==1&&published.motionSource==1,"Stale desktop overwrote panel pacing/NR choices or publication snapshot");
    auto old=desktop;desktop.enabled=false;launcher::transactChoices(file,old,desktop);
    require(launcher::load(file).nr[xpfsr::nr::Intensity]==42&&launcher::load(file).fgPacing==1&&launcher::load(file).fgCap==1&&launcher::load(file).motionSource==1,"Second unrelated desktop save overwrote panel pacing/NR choices");
    require(launcher::readSetting(file,L"Unrelated",L"Keep")==L"yes","Unrelated section lost");
    auto current=launcher::load(file),bad=current;bad.arguments=L"bad\nargument";
    bool rejected=false;try{launcher::transactChoices(file,current,bad);}catch(...){rejected=true;}
    require(rejected&&launcher::serialized(launcher::load(file))==launcher::serialized(current),"Failed transaction changed saved choices");
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::wstring event=L"Local\\XPFSRSettingsTest-"+std::to_wstring(GetCurrentProcessId());HANDLE start=CreateEventW(nullptr,TRUE,FALSE,event.c_str());require(start!=nullptr,"Barrier create");
    PROCESS_INFORMATION children[2]{};
    for(int i=0;i<2;++i){std::wstring command=L"\""+std::wstring(executable)+L"\" \""+root.wstring()+L"\" "+std::to_wstring(i)+L" \""+event+L"\"";STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&children[i])!=FALSE,"Concurrent writer process");CloseHandle(children[i].hThread);}
    SetEvent(start);
    for(auto &child:children){require(WaitForSingleObject(child.hProcess,20000)==WAIT_OBJECT_0,"Writer completion");DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);require(code==0,"Concurrent writer failed");}CloseHandle(start);
    auto after=launcher::load(file);require(after.nr[xpfsr::nr::Intensity]==69&&after.nr[xpfsr::nr::Scope]==1&&after.frameGeneration==3&&after.fgPacing==1&&after.fgCap==1&&!after.enabled,"Cross-process edits were lost");
    for(const auto &p:graphics::fs::directory_iterator(root))require(p.path().extension()==L".ini","Transaction staging file remains");
    std::cout<<"PASS: stale-editor merge, publish snapshot, unrelated sections, rollback and 100 cross-process NR edits preserve settings.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
