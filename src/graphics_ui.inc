// SPDX-License-Identifier: MIT
#include "graphics_presets.hpp"
static HWND msaaBox{}, fxaaBox{}, scaleBox{}, outputBox{}, graphicsStatus{};
static std::vector<const graphics::Preset *> visiblePresets;
static void graphicsMessage(const std::wstring &text, const wchar_t *compact = nullptr) {
    if (graphicsStatus) {
        auto summary = compact ? std::wstring(compact) : text;
        auto at = summary.find(L"\r\n");
        if (at != std::wstring::npos)
            summary.replace(at, 2, L" · ");
        SetWindowTextW(graphicsStatus, summary.c_str());
    }
    if (!running())
        SetWindowTextW(statusBox, text.c_str());
}
static void rememberGraphicsBackup(const fs::path &backup) {
    if (!backup.empty())
        ensure(WritePrivateProfileStringW(L"Graphics", L"LastBackup", backup.c_str(),
                                          (appDirectory / L"settings.ini").c_str()) != FALSE,
               "Graphics were applied, but the backup location could not be saved in settings.ini. "
               "The backup remains under graphics-backups.");
}
static void showGraphicsOptions(const graphics::Options &o) {
    SendMessageW(msaaBox, CB_SETCURSEL, o.msaa, 0);
    SendMessageW(fxaaBox, BM_SETCHECK, o.fxaa ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(scaleBox, CB_SETCURSEL, o.scale, 0);
    size_t selection = 0;
    for (size_t i = 1; i < graphics::outputChoices.size(); ++i)
        if (graphics::outputChoices[i].width == o.outputWidth &&
            graphics::outputChoices[i].height == o.outputHeight)
            selection = i;
    SendMessageW(outputBox, CB_SETCURSEL, selection, 0);
}
static graphics::Options readGraphicsOptions() {
    graphics::Options o;
    o.msaa = int(SendMessageW(msaaBox, CB_GETCURSEL, 0, 0));
    o.fxaa = SendMessageW(fxaaBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.scale = int(SendMessageW(scaleBox, CB_GETCURSEL, 0, 0));
    auto selected = SendMessageW(outputBox, CB_GETCURSEL, 0, 0);
    ensure(selected >= 0 && size_t(selected) < graphics::outputChoices.size(),
           "Choose an output resolution");
    auto &choice = graphics::outputChoices[size_t(selected)];
    o.outputWidth = choice.width;
    o.outputHeight = choice.height;
    return o;
}
static void updatePresetSummary() {
    if (!presetNote)
        return;
    if (preferences.presetId.empty()) {
        SetWindowTextW(
            presetNote,
            (preferences.manualApplied || preferences.manualPending)
                ? L"Custom graphics remembered. Apply graphics with X-Plane closed. FSR / FG stay "
                  L"selected."
                : L"Choose a preset to configure output, AA and FSR automatically. FG defaults "
                  L"Off.");
        return;
    }
    auto &p = graphics::presetById(preferences.presetId);
    std::wstring summary = L"Saved: ";
    summary += p.label;
    summary += L". MSAA / FXAA Off. Output " + std::to_wstring(p.settings.outputWidth) + L" x " +
               std::to_wstring(p.settings.outputHeight) + L" fullscreen.";
    SetWindowTextW(presetNote, summary.c_str());
}
static void syncPresetSelection() {
    size_t selected = 0;
    for (size_t i = 0; i < visiblePresets.size(); ++i)
        if (preferences.presetId == visiblePresets[i]->id)
            selected = i + 1;
    SendMessageW(presetBox, CB_SETCURSEL, selected, 0);
    updatePresetSummary();
}
static void populatePresets() {
    visiblePresets = graphics::availableResolutionPresets(
        [](int w, int h) { return graphics::supportsOutput(w, h); });
    // Preserve a remembered choice even if a monitor has since been disconnected.
    if (!preferences.presetId.empty()) {
        auto *saved = &graphics::presetById(preferences.presetId);
        if (std::find(visiblePresets.begin(), visiblePresets.end(), saved) == visiblePresets.end())
            visiblePresets.push_back(saved);
    }
    SendMessageW(presetBox, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Custom - graphics controls on this screen"));
    for (auto *p : visiblePresets)
        SendMessageW(presetBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p->label));
    SendMessageW(presetBox, CB_SETMINVISIBLE, 12, 0);
    syncPresetSelection();
}
static void clearPreset() {
    preferences.presetId.clear();
    syncPresetSelection();
}
static void selectResolutionPreset() {
    auto selected = SendMessageW(presetBox, CB_GETCURSEL, 0, 0);
    if (selected == 0) {
        launcher::customGraphics(preferences, readGraphicsOptions(), !preferences.manualPending);
        syncPresetSelection();
        saveChoices();
        return;
    }
    ensure(selected > 0 && size_t(selected) <= visiblePresets.size(),
           "Invalid resolution preset selection");
    auto &preset = *visiblePresets[size_t(selected) - 1];
    auto backup = graphics::apply(fs::path(text(pathBox)), appDirectory / L"graphics-backups",
                                  preset.settings);
    rememberGraphicsBackup(backup);
    preferences.presetId = preset.id;
    preferences.manualApplied = preferences.manualPending = false;
    showGraphicsOptions(preset.settings);
    SendMessageW(modeBox, CB_SETCURSEL, preset.launcherMode, 0);
    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
    saveChoices();
    updatePresetSummary();
    std::wstring result = L"Applied and saved: ";
    result += preset.label;
    result += L".\r\nMSAA / FXAA Off; frame generation Off. Launch X-Plane with FSR to use it.\r\n";
    result += backup.empty() ? L"Simulator settings already matched."
                             : L"Previous simulator settings were backed up.";
    graphicsMessage(result, L"Preset saved. FG / MSAA / FXAA Off.");
}
static void applyRememberedGraphics(const fs::path &root) {
    ensure(!running(), "X-Plane is already running from this launcher");
    if (!preferences.presetId.empty())
        rememberGraphicsBackup(
            graphics::apply(root, appDirectory / L"graphics-backups",
                            graphics::presetById(preferences.presetId).settings));
    else if (preferences.manualApplied || preferences.manualPending)
        rememberGraphicsBackup(
            graphics::apply(root, appDirectory / L"graphics-backups", preferences.manual));
    if (preferences.manualPending) {
        preferences.manualApplied = true;
        preferences.manualPending = false;
        saveChoices();
    }
}
static void restoreGraphics() {
    auto backup = launcher::readSetting(appDirectory / L"settings.ini", L"Graphics", L"LastBackup");
    ensure(!backup.empty(), "No graphics backup has been created by this launcher");
    graphics::restore(fs::path(text(pathBox)), fs::path(backup));
    preferences.manualApplied = preferences.manualPending = false;
    preferences.manual = graphics::current(fs::path(text(pathBox)));
    clearPreset();
    showGraphicsOptions(preferences.manual);
    saveChoices();
    graphicsMessage(L"Previous graphics restored. FSR and frame gen choices are unchanged. "
                    L"Check rendering scale before launching.",
                    L"Previous graphics restored.\r\nFSR / FG unchanged.");
}
static void editGraphics() {
    launcher::customGraphics(preferences, readGraphicsOptions(), false);
    syncPresetSelection();
    saveChoices();
    graphicsMessage(L"Choices remembered; simulator changes pending. Click Apply graphics "
                    L"with X-Plane closed. FSR and frame gen are unchanged.",
                    L"Edits saved; apply before launch.\r\nFSR / FG unchanged.");
}
static void applyCustomGraphics() {
    auto options = readGraphicsOptions();
    auto backup =
        graphics::apply(fs::path(text(pathBox)), appDirectory / L"graphics-backups", options);
    rememberGraphicsBackup(backup);
    launcher::customGraphics(preferences, options, true);
    syncPresetSelection();
    saveChoices();
    graphicsMessage(L"Graphics applied and saved. FSR and frame gen selections are unchanged. "
                    L"FSR requires FXAA Off and a matching rendering scale. MSAA is experimental.",
                    L"Graphics saved. FSR / FG unchanged.\r\nFXAA Off for FSR; MSAA preview.");
}
static void createGraphicsControls(HWND parent) {
    msaaBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 200, 180, 110);
    for (auto *v :
         {L"Off · Recommended", L"2x MSAA · Preview", L"4x MSAA · Preview", L"8x MSAA · Preview"})
        SendMessageW(msaaBox, CB_ADDSTRING, 0, LPARAM(v));
    fxaaBox =
        control(L"BUTTON", L"FXAA · Native only", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 200, 32, 111);
    scaleBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 200, 180, 112);
    for (auto *v : {L"Full · Native AA", L"Ultra quality", L"Quality · 1.5x", L"Balanced",
                    L"Performance · 2x"})
        SendMessageW(scaleBox, CB_ADDSTRING, 0, LPARAM(v));
    outputBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 200, 180, 113);
    for (auto &choice : graphics::outputChoices)
        SendMessageW(outputBox, CB_ADDSTRING, 0, LPARAM(choice.label));
    graphics::Options options{};
    if (!preferences.presetId.empty())
        options = graphics::presetById(preferences.presetId).settings;
    else if (preferences.manualApplied || preferences.manualPending)
        options = preferences.manual;
    else {
        try {
            options = graphics::current(fs::path(text(pathBox)));
        } catch (...) {
        }
    }
    showGraphicsOptions(options);
    control(L"BUTTON", L"Apply graphics", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 200, 34, 106);
    graphicsStatus = control(L"STATIC",
                             preferences.manualPending
                                 ? L"Edits saved · Apply graphics before launch"
                                 : L"Custom edits retain FSR and frame generation selections",
                             0, 0, 0, 600, 22, 538);
}
