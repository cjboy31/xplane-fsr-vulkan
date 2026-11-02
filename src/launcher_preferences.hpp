// SPDX-License-Identifier: MIT
#pragma once
#include "graphics_presets.hpp"
#include "nr_controls.hpp"
#include "settings_lock.hpp"
#include <cstring>
namespace launcher {
struct Preferences {
    std::wstring simulator, arguments;
    std::string presetId;
    int mode{}, frameGeneration{}, neuralRendering{};
    int lastFrameGeneration{2};
    int fgPacing{0}; // v2: 0 Smooth (AMD default), 1 Recover after heavy scenes, 2 Power saver.
    int fgCap{0};    // v2: index into xpfsr::pacing::capSettings (0 Off, 1 Auto, then fixed FPS).
    int motionSource{0}; // v2: 0 camera-derived (default), 1 native renderer SSR (experimental).
    int cpuScheduling{0}; // v2: 0 Windows decides (default), 1 opt-in CPU scheduling helper.
    int antiLag{0};       // v2: 0 Off (default), 1 AMD Anti-Lag 2 via VK_AMD_anti_lag when available.
    int fgGpu{0};         // v2: 0 same GPU as X-Plane, 1 second GPU (monitor on it) generates and shows frames.
    int textureBias{0};   // v2: 0 X-Plane default, 1 +0.5 level, 2 AMD-recommended +1 level of texture detail.
    xpfsr::nr::Controls nr;
    bool keepSimUISize{true};
    bool showFrameCounter{};
    bool enabled{true}, ghostReduction{true}, cockpitRestore{true}, alignmentTint{};
    graphics::Options manual{};
    bool manualApplied{}, manualPending{};
};
// Launch reapplies reconstruction inputs, never the saved display geometry.
// A deliberate preset selection / Apply graphics remains the only output edit.
inline graphics::Options graphicsForLaunch(const Preferences &p) {
    graphics::require(!p.manualPending,
                      "Click Apply graphics with X-Plane closed before launching. "
                      "Launch preserves the simulator's current display and UI size.");
    auto o = p.presetId.empty() ? p.manual : graphics::presetById(p.presetId).settings;
    o.outputWidth = o.outputHeight = 0;
    o.cloudQuality=-1; // Only an explicit Apply changes the simulator's cloud-quality slider.
    return o;
}
// Custom output/AA edits never select a reconstruction or FG provider for the user.
inline void customGraphics(Preferences &p, const graphics::Options &options, bool applied) {
    p.manual = options;
    p.presetId.clear();
    p.manualApplied = applied;
    p.manualPending = !applied;
}
inline std::wstring readSetting(const graphics::fs::path &file, const wchar_t *section,
                                const wchar_t *key, const wchar_t *fallback = L"") {
    wchar_t value[32768]{};
    GetPrivateProfileStringW(section, key, fallback, value, 32768, file.c_str());
    return value;
}
inline int integer(const graphics::fs::path &file, const wchar_t *key, int fallback, int maximum,
                   const wchar_t *section = L"Launcher") {
    auto value = readSetting(file, section, key, std::to_wstring(fallback).c_str());
    try {
        size_t used{};
        int n = std::stoi(value, &used);
        return used == value.size() && n >= 0 && n <= maximum ? n : fallback;
    } catch (...) {
        return fallback;
    }
}
inline std::wstring encode(const std::wstring &s) {
    std::wstring result;
    for (unsigned short c : s)
        for (int shift : {12, 8, 4, 0})
            result += L"0123456789abcdef"[(c >> shift) & 15];
    return result;
}
inline std::wstring decode(const std::wstring &s, const std::wstring &fallback) {
    if (s.size() % 4)
        return fallback;
    std::wstring result;
    for (size_t i = 0; i < s.size(); i += 4) {
        unsigned value = 0;
        for (size_t j = 0; j < 4; ++j) {
            auto digit = std::wstring(L"0123456789abcdef").find(s[i + j]);
            if (digit == std::wstring::npos)
                return fallback;
            value = value * 16 + unsigned(digit);
        }
        if (!value || value == '\r' || value == '\n')
            return fallback;
        result += wchar_t(value);
    }
    return result;
}
inline Preferences load(const graphics::fs::path &file) {
    app::NamedLock snapshot(settingsLockName(file));
    Preferences p;
    auto legacy = readSetting(file, L"Application", L"Simulator", p.simulator.c_str());
    p.simulator = readSetting(file, L"Launcher", L"Simulator", legacy.c_str());
    p.arguments = readSetting(file, L"Launcher", L"Arguments");
    p.simulator = decode(
        readSetting(file, L"Launcher", L"SimulatorHex", encode(p.simulator).c_str()), p.simulator);
    p.arguments = decode(
        readSetting(file, L"Launcher", L"ArgumentsHex", encode(p.arguments).c_str()), p.arguments);
    p.mode = integer(file, L"Mode", 0, 4);
    p.frameGeneration = integer(file, L"FrameGeneration", 0, 4);
    p.lastFrameGeneration=integer(file,L"LastFrameGeneration",p.frameGeneration?p.frameGeneration:2,4);
    if(!p.lastFrameGeneration)p.lastFrameGeneration=2;
    p.fgPacing=integer(file,L"FramePacing",0,2);
    p.fgCap=integer(file,L"FrameRateCap",0,11);
    p.motionSource=integer(file,L"MotionSource",0,1);
    p.showFrameCounter=integer(file,L"ShowFrameCounter",0,1)!=0;
    p.cpuScheduling=integer(file,L"CpuScheduling",0,1);
    p.antiLag=integer(file,L"AntiLag",0,1);
    p.textureBias=integer(file,L"TextureBias",0,2);
    p.fgGpu=integer(file,L"FrameGenerationGpu",0,1);
    // Older Normal releases replace their Launcher section on save. Keep NR
    // independently, migrating the unreleased prototype's keys on first save.
    const bool separateNR=readSetting(file,L"NeuralRendering",L"Schema")==L"1";
    const auto nrSection=separateNR?L"NeuralRendering":L"Launcher";
    p.neuralRendering=integer(file,separateNR?L"Mode":L"NeuralRendering",0,4,nrSection);
    p.keepSimUISize=integer(file,L"KeepSimUISize",1,1);
    for(int i=0;i<xpfsr::nr::Count;++i){
        auto &setting=xpfsr::nr::settings[i];
        try{size_t used{};auto s=readSetting(file,nrSection,setting.key,std::to_wstring(setting.initial).c_str());int n=std::stoi(s,&used);if(used==s.size()&&n>=setting.minimum&&n<=setting.maximum)p.nr.value[i]=n;}catch(...){}
    }
    if(!p.nr.valid())p.nr=xpfsr::nr::Controls{};
    p.nr[xpfsr::nr::Enabled]=p.neuralRendering!=0;
    p.enabled = integer(file, L"Enabled", 1, 1);
    p.ghostReduction = integer(file, L"GhostReduction", 1, 1);
    p.cockpitRestore = integer(file, L"CockpitRestore", 1, 1);
    p.alignmentTint = integer(file, L"AlignmentTint", 0, 1);
    auto id = readSetting(file, L"Launcher", L"Preset");
    for (auto &preset : graphics::resolutionPresets)
        if (id == std::wstring(preset.id, preset.id + strlen(preset.id)) &&
            p.mode == preset.launcherMode)
            p.presetId = preset.id;
    p.manual.msaa = integer(file, L"MSAA", 0, 3);
    p.manual.fxaa = integer(file, L"FXAA", 0, 1);
    p.manual.scale = integer(file, L"Scale", 0, 4);
    p.manual.cloudQuality = integer(file,L"CloudQuality",0,4)-1;
    p.manual.outputWidth = integer(file, L"OutputWidth", 0, 16384);
    p.manual.outputHeight = integer(file, L"OutputHeight", 0, 16384);
    bool outputValid = false;
    for (auto &o : graphics::outputChoices)
        outputValid |= o.width == p.manual.outputWidth && o.height == p.manual.outputHeight;
    p.manualApplied = outputValid && integer(file, L"ManualApplied", 0, 1);
    p.manualPending = outputValid && integer(file, L"ManualPending", 0, 1);
    return p;
}
inline std::wstring serialized(const Preferences &p) {
    std::wstring section;
    auto add = [&](const wchar_t *key, const std::wstring &value) {
        graphics::require(value.find_first_of(L"\r\n") == std::wstring::npos &&
                              value.find(L'\0') == std::wstring::npos,
                          "A saved setting contains an unsupported newline or null character");
        section += key;
        section += L'=';
        section += value;
        section += L'\0';
    };
    // Encoded UTF-16 also survives an existing ANSI INI and profile-API quote trimming.
    add(L"SimulatorHex", encode(p.simulator));
    add(L"ArgumentsHex", encode(p.arguments));
    add(L"Preset", std::wstring(p.presetId.begin(), p.presetId.end()));
    add(L"Mode", std::to_wstring(p.mode));
    add(L"FrameGeneration", std::to_wstring(p.frameGeneration));
    add(L"LastFrameGeneration",std::to_wstring(p.lastFrameGeneration));
    add(L"FramePacing",std::to_wstring(p.fgPacing));
    add(L"FrameRateCap",std::to_wstring(p.fgCap));
    add(L"MotionSource",std::to_wstring(p.motionSource));
    add(L"ShowFrameCounter",std::to_wstring(p.showFrameCounter));
    add(L"CpuScheduling",std::to_wstring(p.cpuScheduling));
    add(L"AntiLag",std::to_wstring(p.antiLag));
    add(L"TextureBias",std::to_wstring(p.textureBias));
    add(L"FrameGenerationGpu",std::to_wstring(p.fgGpu));
    add(L"NeuralRendering",std::to_wstring(p.neuralRendering));
    add(L"KeepSimUISize",std::to_wstring(p.keepSimUISize));
    for(int i=0;i<xpfsr::nr::Count;++i)add(xpfsr::nr::settings[i].key,std::to_wstring(i==xpfsr::nr::Enabled?int(p.neuralRendering!=0):p.nr.value[i]));
    add(L"Enabled", std::to_wstring(p.enabled));
    add(L"GhostReduction", std::to_wstring(p.ghostReduction));
    add(L"CockpitRestore", std::to_wstring(p.cockpitRestore));
    add(L"AlignmentTint", std::to_wstring(p.alignmentTint));
    add(L"MSAA", std::to_wstring(p.manual.msaa));
    add(L"FXAA", std::to_wstring(p.manual.fxaa));
    add(L"Scale", std::to_wstring(p.manual.scale));
    add(L"CloudQuality",std::to_wstring(p.manual.cloudQuality+1));
    add(L"OutputWidth", std::to_wstring(p.manual.outputWidth));
    add(L"OutputHeight", std::to_wstring(p.manual.outputHeight));
    add(L"ManualApplied", std::to_wstring(p.manualApplied));
    add(L"ManualPending", std::to_wstring(p.manualPending));
    section += L'\0';
    graphics::require(section.size() < 32767, "Saved choices exceed the settings.ini size limit");
    return section;
}
inline void save(const graphics::fs::path &file, const Preferences &p) {
    // UTF-16 preserves Unicode paths on Windows; existing sections (backups) survive.
    if (!graphics::fs::exists(file))
        graphics::write(file, std::string("\xff\xfe", 2));
    auto section = serialized(p);
    graphics::require(
        WritePrivateProfileSectionW(L"Launcher", section.c_str(), file.c_str()) != FALSE,
        "Could not save choices to settings.ini; choose a writable application folder");
    std::wstring nrSection=L"Schema=1";nrSection+=L'\0';
    nrSection+=L"Mode="+std::to_wstring(p.neuralRendering);nrSection+=L'\0';
    for(int i=0;i<xpfsr::nr::Count;++i){
        nrSection+=xpfsr::nr::settings[i].key;nrSection+=L'=';
        nrSection+=std::to_wstring(i==xpfsr::nr::Enabled?int(p.neuralRendering!=0):p.nr.value[i]);nrSection+=L'\0';
    }
    nrSection+=L'\0';
    graphics::require(WritePrivateProfileSectionW(L"NeuralRendering",nrSection.c_str(),file.c_str())!=FALSE,
                      "Could not save NR choices to settings.ini");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    graphics::require(serialized(load(file)) == section,
                      "Saved choices could not be verified; settings.ini may not be writable");
}
} // namespace launcher
