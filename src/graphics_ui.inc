// SPDX-License-Identifier: MIT
#include "graphics_presets.hpp"
static HWND graphicsWindow{}, presetBox{}, msaaBox{}, fxaaBox{}, scaleBox{}, outputBox{},
    graphicsStatus{};
static std::vector<const graphics::Preset *> visiblePresets;
static void graphicsMessage(const std::wstring &text) {
    SetWindowTextW(graphicsStatus, text.c_str());
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
static void selectResolutionPreset() {
    auto selected = SendMessageW(presetBox, CB_GETCURSEL, 0, 0);
    if (selected <= 0)
        return;
    ensure(size_t(selected) <= visiblePresets.size(), "Invalid resolution preset selection");
    auto &preset = *visiblePresets[size_t(selected) - 1];
    auto backup = graphics::apply(fs::path(text(pathBox)), appDirectory / L"graphics-backups",
                                  preset.settings);
    // Reflect the selected renderer only after preference changes succeed.
    showGraphicsOptions(preset.settings);
    SendMessageW(modeBox, CB_SETCURSEL, preset.launcherMode, 0);
    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
    rememberGraphicsBackup(backup);
    std::wstring result = L"Applied: ";
    result += preset.label;
    result += L".\r\nOutput " + std::to_wstring(preset.settings.outputWidth) + L"x" +
              std::to_wstring(preset.settings.outputHeight) +
              L" fullscreen; MSAA/FXAA Off; SR 3.1.6; FG Off.\r\n";
    result += backup.empty() ? L"Settings already matched; no new backup needed."
                             : L"Previous settings backed up. Restore last change is available.";
    result += L" Close this window, then Launch selected mode.";
    graphicsMessage(result);
}
static LRESULT CALLBACK graphicsProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_COMMAND:
        try {
            auto id = LOWORD(w);
            auto event = HIWORD(w);
            if (id == 109 &&
                (event == CBN_SELENDOK ||
                 (event == CBN_SELCHANGE && !SendMessageW(presetBox, CB_GETDROPPEDSTATE, 0, 0)))) {
                try {
                    selectResolutionPreset();
                } catch (...) {
                    SendMessageW(presetBox, CB_SETCURSEL, 0, 0);
                    throw;
                }
                return 0;
            }
            if (((id == 110 || id == 112 || id == 113) && event == CBN_SELCHANGE) ||
                (id == 111 && event == BN_CLICKED)) {
                SendMessageW(presetBox, CB_SETCURSEL, 0, 0);
                graphicsMessage(L"Manual changes pending. Click Apply manual settings to write "
                                L"them. Temporal FSR needs MSAA and FXAA Off.");
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
                SendMessageW(presetBox, CB_SETCURSEL, 0, 0);
                graphicsMessage(backup.empty()
                                    ? L"These settings are already applied. No preferences changed."
                                    : L"Manual settings applied and backed up. Match the renderer "
                                      L"mode in the main window before launching. Temporal FSR "
                                      L"requires MSAA and FXAA Off.");
                break;
            }
            case 107: {
                wchar_t backup[32768]{};
                GetPrivateProfileStringW(L"Graphics", L"LastBackup", L"", backup, 32768,
                                         (appDirectory / L"settings.ini").c_str());
                ensure(*backup, "No graphics backup has been created by this launcher");
                auto root = fs::path(text(pathBox));
                graphics::restore(root, fs::path(backup));
                SendMessageW(presetBox, CB_SETCURSEL, 0, 0);
                showGraphicsOptions(graphics::current(root));
                graphicsMessage(
                    L"Restored the last changed graphics keys and preserved unrelated settings. "
                    L"Choose a matching renderer before launching, or select another preset.");
                break;
            }
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
        visiblePresets.clear();
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
    visiblePresets = graphics::availableResolutionPresets(
        [](int width, int height) { return graphics::supportsOutput(width, height); });
    auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW c{};
    c.lpfnWndProc = graphicsProcedure;
    c.hInstance = instance;
    c.lpszClassName = L"XPFSR24Graphics";
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&c);
    graphicsWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName,
                                     L"Resolution presets and graphics setup",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                     CW_USEDEFAULT, 710, 610, window, nullptr, instance, nullptr);
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
        L"Choose a preset from the list to apply immediately, with a backup.\r\nX-Plane must be "
        L"closed. Only outputs supported by your primary display appear.",
        0, 20, 15, 660, 50);
    add(L"STATIC", L"Resolution preset", 0, 20, 86, 150, 24);
    presetBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 80, 475, 265, 109);
    SendMessageW(presetBox, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Choose a preset to apply automatically..."));
    for (auto *preset : visiblePresets)
        SendMessageW(presetBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset->label));
    SendMessageW(presetBox, CB_SETCURSEL, 0, 0);
    add(L"STATIC", L"Manual settings", 0, 20, 127, 630, 24);
    add(L"STATIC", L"MSAA", 0, 20, 168, 150, 24);
    msaaBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 162, 475, 120, 110);
    for (auto *v : {L"Off", L"2x MSAA", L"4x MSAA", L"8x MSAA"})
        SendMessageW(msaaBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    fxaaBox = add(L"BUTTON", L"Enable FXAA (native renderer only)", BS_AUTOCHECKBOX | WS_TABSTOP,
                  185, 204, 475, 25, 111);
    add(L"STATIC", L"Rendering scale", 0, 20, 250, 150, 24);
    scaleBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 244, 475, 160, 112);
    for (auto *v : {L"Full - for Native AA", L"Ultra quality", L"Quality - 1.5x upscaling",
                    L"Balanced", L"Performance - 2x upscaling"})
        SendMessageW(scaleBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
    add(L"STATIC", L"Output resolution", 0, 20, 297, 150, 24);
    outputBox = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 185, 292, 475, 150, 113);
    for (auto &choice : graphics::outputChoices)
        SendMessageW(outputBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    showGraphicsOptions(current);
    graphicsStatus =
        add(L"EDIT",
            visiblePresets.empty()
                ? L"No supported preset output was found. Manual AA/scale settings remain "
                  L"available. Windows display modes and drivers are not changed."
                : L"Choose a resolution preset above. It configures fullscreen output, rendering "
                  L"scale, MSAA/FXAA Off, the matching FSR 3.1.6 mode and FG Off. No Apply click "
                  L"is needed for presets. Manual controls use the button below.",
            ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 344, 640, 135);
    add(L"BUTTON", L"Apply manual settings", BS_PUSHBUTTON | WS_TABSTOP, 20, 500, 215, 36, 106);
    add(L"BUTTON", L"Restore last change", BS_PUSHBUTTON | WS_TABSTOP, 250, 500, 195, 36, 107);
    add(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 460, 500, 200, 36, 108);
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
