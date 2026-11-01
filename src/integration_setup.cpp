// SPDX-License-Identifier: MIT
// Installer/background support. No desktop dashboard or special launch step.
#include "layer_registration.hpp"
#include "plugin_install.hpp"
#include "installation_link.hpp"
#include "deferred_graphics.hpp"
#include "session_export.hpp"
#include "nr_import_setup.hpp"
#include "installer_cleanup.hpp"
#include "report_ui.hpp"
#include <iostream>
int wmain(int argc,wchar_t **argv){
    std::filesystem::path root=app::processExecutable().parent_path();
    try{
        graphics::require(argc>=1&&argc<=5,"Usage: IntegrationSetup --repair-registration | --enable [X-Plane folder] | --disable | --uninstall-integration | --apply-graphics-after PID SESSION | --import-nr PID SESSION JOB");
        const std::wstring action=argc==1?L"--repair-registration":argv[1];app::LayerRegistration registration;
        if(action==L"--retire-media"){
            graphics::require(argc==3,"Previous payload inventory required");app::retireOwnedMedia(root,argv[2]);return 0;
        }
        if(action==L"--retire-guide"){
            graphics::require(argc==4,"Previous guide version and SHA-256 required");
            app::retirePreviousGuide(root,argv[2],argv[3],updates::version(updates::currentVersion).key()>=updates::version("0.7.4-alpha").key());return 0;
        }
        if(action==L"--repair-registration"){
            graphics::require(argc<=2,"Registration repair does not accept another installation path");
            auto p=launcher::load(root/L"settings.ini");
            app::verifyRuntime(root/L"runtime",true);
            graphics::require(app::fileHash(root/L"plugin/64/win.xpl")==app::fileHash(std::filesystem::path(p.simulator)/L"Resources/plugins/XPFSR24/64/win.xpl"),"The installed plugin does not match this package. Close X-Plane and use the installer to update it");
            registration.repair(root);
            app::writeInstallationLink(root,p.simulator);
            session::write(root/L"logs/integration-repair.txt","Registration repaired in this desktop user's registry view. Saved choices retained. Restart X-Plane using the usual shortcut.\n");
            if(argc==1)MessageBoxW(nullptr,L"In-sim integration registration repaired.\n\nClose X-Plane normally, then reopen it using your usual shortcut. Your saved settings and models were retained.",L"FSR in-sim integration",MB_OK|MB_ICONINFORMATION);
            return 0;
        }
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
        if(action==L"--finish-session"){
            graphics::require(argc==4,"Simulator PID and session token required");size_t used{};auto pid=std::stoul(argv[2],&used);
            graphics::require(used==wcslen(argv[2])&&pid<=MAXDWORD,"Invalid simulator PID");
            app::finishSession(root,DWORD(pid),argv[3],[&]{support_report::sendQueued(root,argv[3],DWORD(pid));});return 0;
        }
        if(action==L"--report-ui"){
            graphics::require(argc==4,"Simulator PID and session token required");size_t used{};auto pid=std::stoul(argv[2],&used);
            graphics::require(used==wcslen(argv[2])&&pid<=MAXDWORD,"Invalid simulator PID");support_report::show(root,DWORD(pid),argv[3]);return 0;
        }
        graphics::require(argc<=3,"Unexpected integration setup argument");
        if(action==L"--uninstall-integration"){
            graphics::require(argc==2,"Uninstall uses this installation's saved simulator only");
            graphics::ensureSimulatorClosed();
            const auto p=launcher::load(root/L"settings.ini");
            registration.disable(root);app::removeOwnedPlugin(root,p.simulator);return 0;
        }
        if(action==L"--disable"){registration.disable(root);return 0;}
        graphics::require(action==L"--enable","Unknown integration setup operation");
        graphics::ensureSimulatorClosed();
        app::NamedLock integrationGuard(app::lockName(app::integrationOwnerKey));
        auto p=launcher::load(root/L"settings.ini");if(argc==3){auto before=p;p.simulator=argv[2];p=launcher::transactChoices(root/L"settings.ini",before,p);}
        graphics::require(std::filesystem::is_regular_file(std::filesystem::path(p.simulator)/L"X-Plane.exe"),"Select the folder containing X-Plane.exe");
        app::verifyRuntime(root/L"runtime",true);
        const auto previous=registration.preflight(root,true); // Refuse conflicts before changing simulator files.
        if(!std::filesystem::exists(xp_renderer::file(p.simulator)))xp_renderer::save(p.simulator,xp_renderer::Choice::fsr);
        app::synchronizeInput(root);
        app::installPlugin(root,p.simulator,previous);registration.enable(root,false,true);
        app::writeInstallationLink(root,p.simulator);
        return 0;
    }catch(const std::exception&e){
        OutputDebugStringA(e.what());std::error_code ec;std::filesystem::create_directories(root/L"logs",ec);
        if(!ec)std::ofstream(root/L"logs/integration-setup.txt")<<e.what()<<'\n';
        if(argc==1)MessageBoxA(nullptr,e.what(),"FSR in-sim integration - setup could not finish",MB_OK|MB_ICONERROR);
        std::cerr<<e.what()<<'\n';return 1;
    }
}
