// SPDX-License-Identifier: MIT
#include "graphics_presets.hpp"
static HWND graphicsWindow{}, msaaBox{}, fxaaBox{}, scaleBox{}, outputBox{}, graphicsStatus{};
static std::vector<const graphics::Preset *> visiblePresets;
static void graphicsMessage(const std::wstring &text) {
    if (graphicsWindow)
        SetWindowTextW(graphicsStatus, text.c_str());
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
static void updatePresetSummary() {
    if (!presetNote)
        return;
    if (preferences.presetId.empty()) {
        SetWindowTextW(
            presetNote,
            preferences.manualApplied
                ? L"Custom graphics saved. Use Advanced graphics to adjust AA, scale or output."
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
                 reinterpret_cast<LPARAM>(L"Custom - use Advanced graphics"));
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
        clearPreset();
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
    preferences.manualApplied = false;
    SendMessageW(modeBox, CB_SETCURSEL, preset.launcherMode, 0);
    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
    saveChoices();
    updatePresetSummary();
    std::wstring result = L"Applied and saved: ";
    result += preset.label;
    result += L".\r\nMSAA / FXAA Off; frame generation Off. Launch X-Plane with FSR to use it.\r\n";
    result += backup.empty() ? L"Simulator settings already matched."
                             : L"Previous simulator settings were backed up.";
    graphicsMessage(result);
}
static void applyRememberedGraphics(const fs::path &root) {
    ensure(!running(), "X-Plane is already running from this launcher");
    if (!preferences.presetId.empty())
        rememberGraphicsBackup(
            graphics::apply(root, appDirectory / L"graphics-backups",
                            graphics::presetById(preferences.presetId).settings));
    else if (preferences.manualApplied)
        rememberGraphicsBackup(
            graphics::apply(root, appDirectory / L"graphics-backups", preferences.manual));
}
static void restoreGraphics() {
    auto backup = launcher::readSetting(appDirectory / L"settings.ini", L"Graphics", L"LastBackup");
    ensure(!backup.empty(), "No graphics backup has been created by this launcher");
    graphics::restore(fs::path(text(pathBox)), fs::path(backup));
    preferences.manualApplied = false;
    clearPreset();
    SendMessageW(modeBox, CB_SETCURSEL, 0, 0);
    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
    if (graphicsWindow)
        showGraphicsOptions(graphics::current(fs::path(text(pathBox))));
    saveChoices();
    graphicsMessage(L"Previous graphics restored. Unrelated settings were preserved. Choose a new "
                    L"preset, or launch native for comparison.");
}
static LRESULT CALLBACK graphicsProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_COMMAND:
        try {
            auto id = LOWORD(w);
            auto event = HIWORD(w);
            if (showHelp(hwnd, id))
                return 0;
            if (((id == 110 || id == 112 || id == 113) && event == CBN_SELCHANGE) ||
                (id == 111 && event == BN_CLICKED)) {
                graphicsMessage(L"Manual changes pending. Click Apply and save to keep them. "
                                L"Temporal FSR needs MSAA and FXAA Off.");
                return 0;
            }
            switch (id) {
            case 106: {
                auto root = fs::path(text(pathBox));
                graphics::Options o;
                o.msaa = int(SendMessageW(msaaBox, CB_GETCURSEL, 0, 0));
                o.fxaa = SendMessageW(fxaaBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                o.scale = int(SendMessageW(scaleBox, CB_GETCURSEL, 0, 0));
                auto output = SendMessageW(outputBox, CB_GETCURSEL, 0, 0);
                ensure(output >= 0 && size_t(output) < graphics::outputChoices.size(),
                       "Choose a valid output resolution");
                auto &choice = graphics::outputChoices[size_t(output)];
                o.outputWidth = choice.width;
                o.outputHeight = choice.height;
                auto backup = graphics::apply(root, appDirectory / L"graphics-backups", o);
                rememberGraphicsBackup(backup);
                preferences.manual = o;
                preferences.manualApplied = true;
                clearPreset();
                // Match Native AA/upscaling to the internal scale, keeping the chosen provider.
                int mode = int(SendMessageW(modeBox, CB_GETCURSEL, 0, 0));
                mode = (o.msaa || o.fxaa) ? 0 : (mode >= 3 ? 3 : 1) + (o.scale ? 1 : 0);
                SendMessageW(modeBox, CB_SETCURSEL, mode, 0);
                if (!mode)
                    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
                saveChoices();
                updatePresetSummary();
                graphicsMessage(
                    L"Custom graphics applied and saved. The matching FSR mode is selected in the "
                    L"main window. Native MSAA / FXAA settings use Diagnostics or Launch native.");
                break;
            }
            case 107:
                restoreGraphics();
                break;
            case 108:
                DestroyWindow(hwnd);
                break;
            }
        } catch (const std::exception &e) {
            MessageBoxW(hwnd, wide(e.what()).c_str(), L"Graphics settings",
                        MB_OK | MB_ICONEXCLAMATION);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        graphicsWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
static void showGraphics() {
    if (graphicsWindow) {
        SetForegroundWindow(graphicsWindow);
        return;
    }
    graphics::ensureSimulatorClosed();
    auto root = fs::path(text(pathBox));
    graphics::validateMappings(root);
    auto current = graphics::current(root);
    if (preferences.manualApplied)
        current = preferences.manual;
    else if (!preferences.presetId.empty())
        current = graphics::presetById(preferences.presetId).settings;
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW c{};
    c.lpfnWndProc = graphicsProcedure;
    c.hInstance = instance;
    c.lpszClassName = L"XPFSR24Graphics";
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&c);
    graphicsWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName,
                                     L"Advanced graphics - custom settings",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                     CW_USEDEFAULT, 710, 510, window, nullptr, instance, nullptr);
    ensure(graphicsWindow != nullptr, "Cannot create graphics settings window");
    auto add = [&](const wchar_t *type, const wchar_t *label, DWORD style, int x, int y, int width,
                   int height, int id = 0) {
        auto h = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                                 graphicsWindow, reinterpret_cast<HMENU>(intptr_t(id)), instance,
                                 nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };
    add(L"STATIC",
        L"Presets are on the main screen. Customize AA, scale or output here.\r\nX-Plane must be "
        L"closed. Click Apply and save to keep manual changes.",
        0, 20, 15, 650, 48);
    add(L"STATIC", L"MSAA", 0, 20, 83, 150, 24);
    msaaBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 77, 475, 120, 110);
    for (auto *v : {L"Off", L"2x MSAA", L"4x MSAA", L"8x MSAA"})
        SendMessageW(msaaBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    fxaaBox = add(L"BUTTON", L"Enable FXAA (native renderer only)", BS_AUTOCHECKBOX | WS_TABSTOP,
                  185, 119, 475, 25, 111);
    add(L"STATIC", L"Rendering scale", 0, 20, 165, 150, 24);
    scaleBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 159, 475, 160, 112);
    for (auto *v : {L"Full - for Native AA", L"Ultra quality", L"Quality - 1.5x upscaling",
                    L"Balanced", L"Performance - 2x upscaling"})
        SendMessageW(scaleBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    add(L"STATIC", L"Output", 0, 20, 212, 120, 24);
    outputBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 207, 475, 150, 113);
    for (auto &choice : graphics::outputChoices)
        SendMessageW(outputBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    showGraphicsOptions(current);
    graphicsStatus = add(L"EDIT",
                         L"Manual edits are pending until Apply and save. Help buttons explain "
                         L"when each setting is useful.",
                         ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 259, 640, 115);
    helpIcon(graphicsWindow, 210, 145, 77);
    helpIcon(graphicsWindow, 211, 145, 119);
    helpIcon(graphicsWindow, 212, 145, 159);
    helpIcon(graphicsWindow, 213, 145, 207);
    add(L"BUTTON", L"Apply and save", BS_PUSHBUTTON | WS_TABSTOP, 20, 400, 215, 36, 106);
    add(L"BUTTON", L"Restore last change", BS_PUSHBUTTON | WS_TABSTOP, 250, 400, 195, 36, 107);
    add(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 460, 400, 200, 36, 108);
    EnableWindow(window, FALSE);
    ShowWindow(graphicsWindow, SW_SHOW);
    UpdateWindow(graphicsWindow);
    MSG msg{};
    while (graphicsWindow && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(graphicsWindow, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(window, TRUE);
    SetForegroundWindow(window);
}
