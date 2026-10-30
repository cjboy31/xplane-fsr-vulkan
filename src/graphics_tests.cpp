// SPDX-License-Identifier: MIT
#include "graphics_presets.hpp"
#include "graphics_settings.hpp"
#include "launcher_preferences.hpp"
#include <iostream>
#include <set>
using namespace graphics;
template <class F> static void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Expected invalid/conflicting graphics operation to fail");
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "Fresh workspace test directory required");
        fs::path base = argv[1];
        require(!fs::exists(base), "Refusing to reuse an existing test directory");
        auto root = base / L"fake-simulator";
        fs::create_directories(prefs(root));
        fs::create_directories(root / L"Resources");
        write(root / L"Resources/settings.txt",
              "SETTING_1 renopt_MSAA 0 hdr/msaa_hw 0\nSETTING_1 renopt_FXAA 0 hdr/use_post_aa "
              "0\nSETTING_1 renopt_FSR_04 2 fsr/enable 1 fsr/quality 2\n");
        const std::string original =
            "I\r\n10 Version\r\nrenopt_MSAA 2\r\nrenopt_FXAA 1\r\nrenopt_FSR_04 0\r\nother_setting "
            "preserve this exactly\r\n";
        const std::string windows =
            "I\n10 Version\nnum_monitors 1\nmonitor/0/m_monitor 0\nmonitor/0/m_x_res_full "
            "2560\nmonitor/0/m_y_res_full 1440\nmonitor/0/m_is_fullscreen "
            "wmgr_mode_windowed\nother_window keep me\n";
        auto main = prefs(root) / L"X-Plane.prf",
             screen = prefs(root) / L"X-Plane Window Positions.prf";
        write(main, original);
        write(screen, windows);
        Options o{0, 0, 2, 3840, 2160};
        auto backup = apply(root, base / L"backups", o, false);
        require(get(read(main), "renopt_MSAA") == "0" && get(read(main), "renopt_FXAA") == "0" &&
                    get(read(main), "renopt_FSR_04") == "2",
                "AA/scale edit");
        require(get(read(screen), "monitor/0/m_x_res_full") == "3840" &&
                    get(read(screen), "monitor/0/m_y_res_full") == "2160" &&
                    get(read(screen), "monitor/0/m_is_fullscreen") == "wmgr_mode_fullscreen",
                "4K output edit");
        require(read(backup / L"X-Plane.prf") == original &&
                    read(backup / L"X-Plane Window Positions.prf") == windows,
                "Exact original backups");
        write(main, set(read(main), "other_setting", "changed after launch"));
        restore(root, backup);
        require(get(read(main), "other_setting") == "changed after launch",
                "Restore preserves unrelated setting edits");
        require(get(read(main), "renopt_MSAA") == "2" && read(screen) == windows,
                "Graphics restore");
        auto duplicate = original + "renopt_MSAA 1\r\n";
        rejects([&] { get(duplicate, "renopt_MSAA"); });
        rejects([&] { get(original, "unknown_key"); });
        write(main, original);
        backup = apply(root, base / L"backups", o, false);
        write(main, set(read(main), "renopt_MSAA", "3"));
        auto changed = read(main), changedScreen = read(screen);
        rejects([&] { restore(root, backup); });
        require(read(main) == changed && read(screen) == changedScreen,
                "Conflicting restore must not partially edit files");
        write(main, original);
        write(screen, windows);
        SetFileAttributesW(main.c_str(), FILE_ATTRIBUTE_READONLY);
        bool failed = false;
        try {
            apply(root, base / L"backups", o, false);
        } catch (...) {
            failed = true;
        }
        SetFileAttributesW(main.c_str(), FILE_ATTRIBUTE_NORMAL);
        require(failed && read(main) == original && read(screen) == windows,
                "Two-file commit rolls back after a failed second write");
        write(screen, set(windows, "num_monitors", "2"));
        rejects([&] { apply(root, base / L"backups", o, false); });
        require(read(main) == original, "Multi-monitor refusal leaves AA preferences unchanged");
        Options invalid{4, 0, 0, 0, 0};
        rejects([&] { apply(root, base / L"backups", invalid, false); });
        auto only1080 = availableResolutionPresets(
            [](int width, int height) { return width == 1920 && height == 1080; });
        require(only1080.size() == 4, "Display filter must hide unsupported 1440p and 4K presets");
        require(availableResolutionPresets([](int, int) { return false; }).empty(),
                "Display with no supported presets");
        require(availableResolutionPresets([](int, int) { return true; }).size() == 14,
                "Seven standard presets for each of FSR 3 and FSR 4");
        rejects([&] { presetById("nonexistent"); });
        std::set<std::string> ids;
        for (auto &preset : resolutionPresets) {
            require(ids.insert(preset.id).second, "Duplicate preset ID");
            require(preset.settings.msaa == 0 && preset.settings.fxaa == 0,
                    "Temporal presets disable MSAA and FXAA");
            require((preset.launcherMode == (preset.settings.scale == 0 ? 1 : 2)) ||
                        (preset.launcherMode == (preset.settings.scale == 0 ? 3 : 4)),
                    "Preset must select matching Native AA/upscaling mode");
            write(main, original);
            write(screen, windows);
            auto saved = apply(root, base / L"backups", preset.settings, false);
            auto actual = current(root);
            require(actual.msaa == 0 && actual.fxaa == 0 && actual.scale == preset.settings.scale,
                    "Preset AA/scale did not reach preferences");
            require(get(read(screen), "monitor/0/m_x_res_full") ==
                            std::to_string(preset.settings.outputWidth) &&
                        get(read(screen), "monitor/0/m_y_res_full") ==
                            std::to_string(preset.settings.outputHeight),
                    "Preset output did not reach preferences");
            restore(root, saved);
            require(read(main) == original && read(screen) == windows,
                    "Preset restore did not preserve exact original files");
            std::cout << "PASS preset: " << preset.id << " output=" << preset.settings.outputWidth
                      << 'x' << preset.settings.outputHeight << " scale=" << preset.settings.scale
                      << '\n';
        }
        // The original launch path restored the preset's fullscreen geometry.
        // Test the same policy used by BOTH FSR and native-comparison buttons.
        auto misc = prefs(root) / L"Miscellaneous.prf";
        const std::string accessibility = "I\r\n1100 Version\r\npixel_multiplier 1.500000\r\nbase_font_size 18\r\n";
        write(misc, accessibility);
        auto ini = base / L"launcher.ini";
        for (auto &preset : resolutionPresets) {
            launcher::Preferences p;
            p.presetId = preset.id;
            p.mode = preset.launcherMode;
            launcher::save(ini, p);
            for (auto screenState : {windows, set(windows, "monitor/0/m_is_fullscreen", "wmgr_mode_fullscreen")}) {
                write(main, original);
                write(screen, screenState);
                for (int restart = 0; restart != 3; ++restart) {
                    auto reopened = launcher::load(ini);
                    auto renderOnly = launcher::graphicsForLaunch(reopened);
                    apply(root, base / L"backups", renderOnly, false);
                    require(read(screen) == screenState && read(misc) == accessibility,
                            "Repeated launches must preserve current window/output/UI bytes");
                    require(current(root).scale == preset.settings.scale &&
                                reopened.presetId == preset.id,
                            "Preserving display must retain saved FSR and input scale");
                }
            }
        }
        launcher::Preferences pending;
        pending.manualPending = true;
        pending.manual = o;
        rejects([&] { launcher::graphicsForLaunch(pending); });
        pending.manualPending = false;
        pending.manualApplied = true;
        auto renderOnly = launcher::graphicsForLaunch(pending);
        require(!renderOnly.outputWidth && !renderOnly.outputHeight && renderOnly.scale == o.scale,
                "Applied custom graphics also preserve display geometry");
        std::cout << "PASS: 84 reopened preset launches preserve window mode, output and UI scale; "
                     "custom outputs stay remembered; unapplied edits require Apply graphics.\n";
        std::cout << "PASS: exact backups, MSAA/FXAA/scale/4K output, selective restore, "
                     "duplicate/missing keys, conflict refusal, multi-monitor refusal and rollback "
                     "after write failure. Only workspace fixture files changed.\n";
        std::cout << "Primary display advertises 4K: " << supportsOutput(3840, 2160) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
