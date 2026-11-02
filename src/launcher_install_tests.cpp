// SPDX-License-Identifier: MIT
// Exercise the real owned-plugin upgrade functions without starting X-Plane.
#define wWinMain unusedLauncherMain
#include "launcher.cpp"
#undef wWinMain
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    try {
        ensure(argc == 4, "launcher_install_tests APP_DIR PREVIOUS_PLUGIN FRESH_FIXTURE");
        fs::path source = argv[1], previous = argv[2], fixture = argv[3];
        ensure(!fs::exists(fixture), "Fresh workspace fixture required");
        const std::string oldHash = hash(previous);
        ensure(oldHash == "370ee0ff5119aba274755d7f0ed67825b338df35b13c8665db8ba4af229e5b96" ||
                   oldHash == "6b0b17f2d315b5d50018dd167e896cd54a814ad11aa03ad6d94d2a14ed882e3a" ||
                   oldHash == "5fb87828f94aca906601703d49f0626aeac85e38ebf7b068b00867cd18535720",
               "Previous plugin is not a verified published binary");
        appDirectory = fixture / L"app";
        for (auto relative :
             {L"layer/XPFSR24Layer.dll", L"layer/XPFSR24Layer.json", L"plugin/64/win.xpl",
              L"runtime/amd_fidelityfx_loader_vk.dll", L"runtime/amd_fidelityfx_upscaler_vk.dll",
              L"runtime/amd_fidelityfx_framegeneration_vk.dll"}) {
            auto target = appDirectory / relative;
            fs::create_directories(target.parent_path());
            fs::copy_file(source / relative, target);
        }
        auto root = fixture / L"simulator", target = root / L"Resources/plugins/XPFSR24/64/win.xpl",
             prefs = root / L"Output/preferences/X-Plane.prf";
        fs::create_directories(target.parent_path());
        fs::create_directories(prefs.parent_path());
        std::ofstream(root / L"X-Plane.exe") << "Inert fixture; never executed";
        std::ofstream(prefs) << "User simulator preferences remain untouched\n";
        auto prefsHash = hash(prefs);
        fs::copy_file(previous, target);
        install(root);
        ensure(hash(target) == hash(appDirectory / L"plugin/64/win.xpl"),
               "Plugin upgrade did not install current metadata publisher");
        auto backup = appDirectory / L"plugin-backups" /
                      (L"camera-previous-" + wide(oldHash.c_str()) + L".xpl");
        ensure(hash(backup) == oldHash && hash(prefs) == prefsHash,
               "Upgrade lost backup or changed simulator preferences");
        install(root); // idempotent current version
        removePlugin(root);
        ensure(!fs::exists(target), "Owned plugin removal failed");
        fs::create_directories(target.parent_path());
        std::ofstream(target) << "Unrecognized third-party fixture";
        auto unknown = hash(target);
        bool refused = false;
        try {
            install(root);
        } catch (...) {
            refused = true;
        }
        ensure(refused && hash(target) == unknown, "Upgrade overwrote an unrecognized plugin");
        std::cout << "PASS: previous owned camera publisher upgrades with verified backup, repeat install, "
                     "owned removal and foreign-file refusal; no simulator was started\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
