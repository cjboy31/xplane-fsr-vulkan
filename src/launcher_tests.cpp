// SPDX-License-Identifier: MIT
#include "launcher_preferences.hpp"
#include "runtime_status.hpp"
#include <iostream>
using graphics::require;
template <class F> static void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Operation should have failed");
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "Fresh fixture folder required");
        graphics::fs::path dir = argv[1];
        require(!graphics::fs::exists(dir), "Refusing to reuse a fixture");
        graphics::fs::create_directories(dir);
        auto file = dir / L"settings.ini";
        auto defaults = launcher::load(file);
        require(defaults.mode == 0 && defaults.enabled && defaults.cockpitRestore &&
                    defaults.frameGeneration == 0 && defaults.neuralRendering == 0 && defaults.presetId.empty(),
                "First-run defaults");
        // Exercise upgrading the ANSI file created by the old launcher/installer.
        graphics::write(file, "[Application]\r\nSimulator=D:\\Simulator "
                              "One\r\n[Graphics]\r\nLastBackup=keep this backup\r\n");
        require(launcher::load(file).simulator == L"D:\\Simulator One", "Legacy folder migration");
        WritePrivateProfileStringW(L"FeatureVideo", L"Automatic", L"0", file.c_str());
        WritePrivateProfileStringW(L"FeatureVideo", L"Muted", L"1", file.c_str());
        WritePrivateProfileStringW(L"FeatureVideo", L"LastPresented", L"0.6.2-alpha", file.c_str());
        for (auto &preset : graphics::resolutionPresets) {
            launcher::Preferences p;
            p.simulator = L"D:\\Simulador \u00e9\u822a\\X-Plane 12";
            p.arguments = L"  \"quoted argument\" --flag=\u00e9 ; trailing  ";
            p.presetId = preset.id;
            p.mode = preset.launcherMode;
            p.frameGeneration = 2;
            p.neuralRendering = 3;
            p.nr[xpfsr::nr::Intensity]=37;p.nr[xpfsr::nr::Tone]=63;p.nr[xpfsr::nr::Structure]=125;
            p.nr[xpfsr::nr::Scale]=33;p.nr[xpfsr::nr::Style]=2;p.nr[xpfsr::nr::Passes]=3;p.nr[xpfsr::nr::AllowFG]=1;
            p.cockpitRestore = false;
            p.alignmentTint = true;
            p.enabled = false;
            p.ghostReduction = false;
            launcher::save(file, p);
            require(launcher::serialized(launcher::load(file)) == launcher::serialized(p),
                    "Every preset/provider/checkbox/Unicode argument must survive restart");
            require(launcher::readSetting(file, L"Graphics", L"LastBackup") == L"keep this backup",
                    "Saving choices must retain backup metadata");
            require(launcher::readSetting(file, L"FeatureVideo", L"Automatic") == L"0" &&
                        launcher::readSetting(file, L"FeatureVideo", L"Muted") == L"1" &&
                        launcher::readSetting(file, L"FeatureVideo", L"LastPresented") == L"0.6.2-alpha",
                    "Saving presets must retain feature-video opt-out, mute and presentation state");
        }
        launcher::Preferences custom;
        custom.manual = {2, 1, 4, 3840, 2160};
        custom.manualApplied = true;
        custom.arguments = L"\"entire argument is quoted\"";
        launcher::save(file, custom);
        require(launcher::serialized(launcher::load(file)) == launcher::serialized(custom),
                "Custom AA/output/quote-only argument persistence");
        for (int mode = 0; mode <= 4; ++mode)
            for (int fg = 0; fg <= 2; ++fg)
                for (int msaa = 0; msaa <= 3; ++msaa) {
                    custom.mode = mode;
                    custom.frameGeneration = fg;
                    custom.presetId = "4k-quality";
                    graphics::Options edit{msaa, 0, 2, 2560, 1440};
                    launcher::customGraphics(custom, edit, false);
                    require(custom.mode == mode && custom.frameGeneration == fg &&
                                custom.presetId.empty(),
                            "Custom edits must never change FSR / FG selections");
                    launcher::save(file, custom);
                    auto pending = launcher::load(file);
                    require(pending.manualPending && !pending.manualApplied &&
                                pending.mode == mode && pending.frameGeneration == fg &&
                                pending.manual.msaa == msaa,
                            "Pending custom edits and providers must survive restart");
                    launcher::customGraphics(pending, edit, true);
                    launcher::save(file, pending);
                    auto applied = launcher::load(file);
                    require(applied.manualApplied && !applied.manualPending &&
                                applied.mode == mode && applied.frameGeneration == fg,
                            "Applied custom edits must preserve providers after restart");
                }
        auto newFile = dir / L"new-settings.ini";
        launcher::save(newFile, custom);
        require(launcher::serialized(launcher::load(newFile)) == launcher::serialized(custom),
                "First-save Unicode INI round trip");
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
        rejects([&] {
            custom.mode = 3;
            launcher::save(file, custom);
        });
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        WritePrivateProfileStringW(L"Launcher", L"Mode", L"999", file.c_str());
        WritePrivateProfileStringW(L"Launcher", L"FrameGeneration", L"nonsense", file.c_str());
        WritePrivateProfileStringW(L"Launcher", L"NeuralRendering", L"999", file.c_str());
        WritePrivateProfileStringW(L"Launcher", L"Preset", L"unknown", file.c_str());
        auto invalid = launcher::load(file);
        require(invalid.mode == 0 && invalid.frameGeneration == 0 && invalid.neuralRendering == 0 && invalid.presetId.empty(),
                "Corrupt saved selectors use safe defaults");
        launcher::RuntimeMonitor monitor;
        using S = launcher::RuntimeState;
        app::Telemetry t{};
        auto state = [&](bool run, bool native, bool available, bool enabled, uint64_t tick) {
            return monitor.update(run, native, available, t, enabled, tick);
        };
        require(state(false, false, true, true, 1000) == S::stopped, "Not running");
        require(state(true, true, true, true, 1000) == S::native,
                "Native launch never claims injection");
        require(state(true, false, true, true, 1000) == S::starting,
                "Empty camera mapping is not injection proof");
        strcpy_s(t.reason, "Layer waiting for flight");
        t.presents = 1;
        require(state(true, false, true, true, 2000) == S::loaded,
                "Layer activity is not FSR processing");
        t.active = 1;
        t.recordings = 1;
        t.presents = 2;
        require(state(true, false, true, true, 3000) == S::active,
                "Fresh FSR dispatch and present confirmation");
        require(state(true, false, true, false, 4000) == S::paused,
                "Disabled processing never green");
        require(state(true, false, true, true, 10001) == S::stalled,
                "Stale telemetry never stays green");
        monitor.reset();
        t.active = 0;
        t.recordings = 0;
        t.presents = 0;
        require(state(true, false, false, true, 11000) == S::starting,
                "Fresh launch waits for fresh evidence");
        t.active = 1;
        t.recordings = 2;
        require(state(true, false, true, true, 12000) == S::loaded,
                "Unpresented command recordings are not active proof");
        std::cout << "PASS: all 14 preset/provider round trips, Unicode/quoted arguments, "
                     "checkboxes, custom AA/output, legacy migration, backup preservation, write "
                     "failure, corrupt selectors and injection-state transitions.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
