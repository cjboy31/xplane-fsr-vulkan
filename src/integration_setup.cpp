// SPDX-License-Identifier: MIT
// Installer/background support. No desktop dashboard or special launch step.
#include "layer_registration.hpp"
#include "plugin_install.hpp"
#include "deferred_graphics.hpp"
#include "nr_import_setup.hpp"
#include <iostream>
int wmain(int argc,wchar_t **argv){
    std::filesystem::path root=app::processExecutable().parent_path();
    try{
        graphics::require(argc>=2&&argc<=5,"Usage: IntegrationSetup --enable [X-Plane folder] | --disable | --apply-graphics-after PID SESSION | --import-nr PID SESSION JOB");
        const std::wstring action=argv[1];app::LayerRegistration registration;
        if(action==L"--import-nr"){
            graphics::require(argc==5,"Simulator PID, session and import job required");size_t used{};auto pid=std::stoul(argv[2],&used);
            graphics::require(used==wcslen(argv[2])&&pid<=MAXDWORD,"Invalid simulator PID");
            app::importNRInSim(root,DWORD(pid),argv[3],argv[4]);return 0;
        }
        if(action==L"--apply-graphics-after"){
            graphics::require(argc==4,"Simulator PID and session token required");size_t used{};auto pid=std::stoul(argv[2],&used);
            graphics::require(used==wcslen(argv[2])&&pid<=MAXDWORD,"Invalid simulator PID");
            app::waitAndApplyGraphics(root,DWORD(pid),argv[3]);return 0;
        }
        graphics::require(argc<=3,"Unexpected integration setup argument");
        if(action==L"--disable"){registration.disable(root);return 0;}
        graphics::require(action==L"--enable","Unknown integration setup operation");
        graphics::ensureSimulatorClosed();
        auto p=launcher::load(root/L"settings.ini");if(argc==3){auto before=p;p.simulator=argv[2];p=launcher::transactChoices(root/L"settings.ini",before,p);}
        graphics::require(std::filesystem::is_regular_file(std::filesystem::path(p.simulator)/L"X-Plane.exe"),"Select the folder containing X-Plane.exe");
        auto previous=app::registryString(HKEY_CURRENT_USER,app::integrationOwnerKey,L"Manifest");
        graphics::require(previous.empty()||!_wcsicmp(previous.c_str(),app::LayerRegistration::manifest(root).c_str()),"Disable automatic activation in the other app installation before enabling it here");
        app::verifyRuntime(root/L"runtime",true);app::installPlugin(root,p.simulator);registration.enable(root);
        return 0;
    }catch(const std::exception&e){
        OutputDebugStringA(e.what());std::error_code ec;std::filesystem::create_directories(root/L"logs",ec);
        if(!ec)std::ofstream(root/L"logs/integration-setup.txt")<<e.what()<<'\n';
        std::cerr<<e.what()<<'\n';return 1;
    }
}
