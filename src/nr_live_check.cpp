// SPDX-License-Identifier: MIT
// Development-only probe for this project's documented NR control channel.
// Does not automate UI, inject code, edit simulator files or persist choices.
#include "nr_controls.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
static void need(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
int wmain(int argc,wchar_t**argv){
    HANDLE process{};
    try{
        need(argc==5,"NRLiveCheck LOG_DIRECTORY EXPECTED_XPLANE_EXE SETTING_KEY VALUE");
        uint64_t pid{},tick{},records{};std::string token,line;
        std::ifstream in(std::filesystem::path(argv[1])/L"renderer-nr-status.txt");
        need(bool(in>>pid>>tick>>records),"No NR session status");std::getline(in,line);std::getline(in,token);
        auto now=GetTickCount64();need(tick<=now&&now-tick<3000&&pid&&pid<=MAXDWORD,"Stale NR session");
        need(token.size()==32&&token.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid session token");
        process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,DWORD(pid));
        need(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT,"Simulator session has exited");
        wchar_t executable[32768]{};DWORD n=32768;
        need(QueryFullProcessImageNameW(process,0,executable,&n),"Cannot verify simulator identity");
        need(!_wcsicmp(std::filesystem::canonical(executable).c_str(),std::filesystem::canonical(argv[2]).c_str()),"Status belongs to another process");
        xpfsr::nr::ControlChannel channel;std::wstring name=L"Local\\XPFSR24-NR-v3-"+std::wstring(token.begin(),token.end());
        need(channel.open(name.c_str()),"No live NR channel");xpfsr::nr::Controls c;need(channel.read(c),"No valid control snapshot");
        int field=-1;for(int i=0;i<xpfsr::nr::Count;++i)if(!wcscmp(argv[3],xpfsr::nr::settings[i].key))field=i;
        need(field>=0,"Unknown setting key");std::wstring v=argv[4];size_t used{};int value=std::stoi(v,&used);need(used==v.size(),"Invalid setting value");
        c.value[field]=value;need(c.valid(),"Setting out of range");channel.write(c);
        std::wcout<<L"PASS: published "<<argv[3]<<L"="<<value<<L" to verified live X-Plane PID "<<pid<<L"; existing recordings="<<records<<L"\n";
        CloseHandle(process);return 0;
    }catch(const std::exception&e){if(process)CloseHandle(process);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
