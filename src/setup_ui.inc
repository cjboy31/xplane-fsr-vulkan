// SPDX-License-Identifier: MIT
// Read-only setup guidance. Opening/checking this window never edits the simulator.
static HWND setupWindow{}, setupText{};
static std::wstring setupReport() {
    std::wostringstream out;
    out << L"SIM SETUP - FIRST FLIGHT\r\n"
        << L"1. Close X-Plane. Select its folder, then choose a preset on the main screen.\r\n"
        << L"2. Start with FSR 3 Native AA and frame generation Off. Presets set MSAA / FXAA "
           L"Off.\r\n"
        << L"3. Launch X-Plane with FSR from this app. Load a flight; look for green FSR "
           L"ACTIVE.\r\n\r\n"
        << L"REQUIRED GRAPHICS SETTINGS\r\n"
        << L"Mode                       Internal rendering scale          MSAA / FXAA\r\n"
        << L"Native AA (FSR 3 or 4)      Full resolution                   FXAA Off; MSAA "
           L"optional\r\n"
        << L"Quality upscaling          Quality (1.5x)                    FXAA Off; MSAA "
           L"optional\r\n"
        << L"Performance upscaling      Performance (2x)                  FXAA Off; MSAA "
           L"optional\r\n\r\n"
        << L"The simulator's Rendering Resolution / FSR scale sets INPUT size. The launcher "
           L"selects the FSR provider. "
           L"Upscaling requires below Full; Native AA requires Full.\r\n"
        << L"OUTPUT size is separate: 4K Quality = 3840 x 2160 fullscreen output from 2560 x 1440 "
           L"input. "
           L"A 1440p output stays 1440p. Choose a display-supported preset.\r\n\r\n"
        << L"FIRST-TEST OPTIONS\r\n"
        << L"Use one primary-display simulator window, 2D (VR Off). This build is Windows Vulkan "
           L"only; "
           L"X-Plane 12 already uses Vulkan on Windows, so no old X-Plane 11 Vulkan checkbox is "
           L"required.\r\n"
        << L"Keep FG Off, cockpit-screen fix Off and alignment tint Off for the first general FSR "
           L"test. "
           L"After that, test the optional cockpit fix only with its supported default Cirrus SR22 "
           L"profile and FG Off. "
           L"Tint is an alignment check, not a normal graphics setting.\r\n"
        << L"Run AA check for the selected provider. FSR 4 needs runtime-supported "
           L"hardware/drivers. "
           L"Avoid simultaneous DLSS/TAA/MotionVectors renderer replacements or a local "
           L"vulkan-1.dll proxy.\r\n"
        << L"Do not force the simulator's internal scale to Full while using an upscaling mode. "
           L"The preset configures it intentionally. Other graphics sliders can be tuned for "
           L"performance; "
           L"FG does not repair a slow or stuttering base frame rate.\r\n\r\n"
        << L"SAVED-SETTINGS CHECK (READ ONLY)\r\n"
        << L"Selected mode: " << text(modeBox) << L"\r\n"
        << L"Frame generation: " << text(fgBox) << L"\r\n";
    auto root = fs::path(text(pathBox));
    try {
        graphics::validateMappings(root);
        auto current = graphics::current(root);
        graphics::Options expected{};
        bool expectedGraphics = false;
        if (!preferences.presetId.empty()) {
            expected = graphics::presetById(preferences.presetId).settings;
            expectedGraphics = true;
        } else if (preferences.manualApplied) {
            expected = preferences.manual;
            expectedGraphics = true;
        }
        int mode = int(SendMessageW(modeBox, CB_GETCURSEL, 0, 0));
        if (mode > 0) {
            out << (current.msaa == 0 ? L"[OK] MSAA Off"
                                      : L"[PREVIEW] MSAA enabled; costs GPU time/memory")
                << L"\r\n"
                << (current.fxaa == 0 ? L"[OK] FXAA Off" : L"[CHANGE] FXAA must be Off") << L"\r\n";
            bool nativeAA = mode == 1 || mode == 3;
            bool matching =
                nativeAA ? current.scale == 0 : current.scale >= 1 && current.scale <= 4;
            out << (matching ? L"[OK] " : L"[CHANGE] ") << L"Internal scale: "
                << (current.scale == 0   ? L"Full"
                    : current.scale == 1 ? L"Ultra quality"
                    : current.scale == 2 ? L"Quality"
                    : current.scale == 3 ? L"Balanced"
                    : current.scale == 4 ? L"Performance"
                                         : L"Unsupported value")
                << (nativeAA ? L" (Native AA needs Full)" : L" (upscaling needs below Full)")
                << L"\r\n";
            if (expectedGraphics && current.scale != expected.scale)
                out << L"[CHANGE] Internal scale differs from your saved preset/custom choice. "
                       L"Reapply it with X-Plane closed.\r\n";
        } else
            out << L"[NOTE] Diagnostics mode does not enable FSR reconstruction. Select a preset "
                   L"for FSR.\r\n";
        auto screen = graphics::read(graphics::prefs(root) / L"X-Plane Window Positions.prf");
        auto width = graphics::get(screen, "monitor/0/m_x_res_full");
        auto height = graphics::get(screen, "monitor/0/m_y_res_full");
        auto fullscreen =
            graphics::get(screen, "monitor/0/m_is_fullscreen") == "wmgr_mode_fullscreen";
        out << L"Saved fullscreen output: " << wide(width.c_str()) << L" x " << wide(height.c_str())
            << (fullscreen ? L" (fullscreen selected)"
                           : L" (windowed; fullscreen size is not the live window size)")
            << L"\r\n";
        if (expectedGraphics && expected.outputWidth)
            out << (width == std::to_string(expected.outputWidth) &&
                            height == std::to_string(expected.outputHeight) && fullscreen
                        ? L"[OK] "
                        : L"[CHANGE] ")
                << L"Saved output target: " << expected.outputWidth << L" x "
                << expected.outputHeight << L" fullscreen\r\n";
        out << (graphics::get(screen, "num_monitors") == "1" &&
                        graphics::get(screen, "monitor/0/m_monitor") == "0"
                    ? L"[OK] Single primary-display simulator window"
                    : L"[CHANGE] Use one simulator window on the primary display for this preview")
            << L"\r\n";
    } catch (const std::exception &e) {
        out << L"[CHECK NEEDED] " << wide(e.what()) << L"\r\n";
    }
    bool fg = SendMessageW(fgBox, CB_GETCURSEL, 0, 0) > 0;
    bool cockpit = SendMessageW(cockpitBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (fg && cockpit)
        out << L"[CHANGE] Cockpit-screen restoration requires FG Off.\r\n";
    out << L"\r\nThis check reads saved preferences only. X-Plane may have unsaved/live settings "
           L"while running. "
           L"It does not install the plugin, change any graphics setting, or prove active "
           L"injection. "
           L"Use the main live-status panel for actual renderer input/output sizes after loading a "
           L"flight.\r\n"
        << L"To correct a mismatch: close X-Plane, select a preset or use the main-screen "
           L"Apply and save, then relaunch. "
           L"Open the full guide for limitations, backups and removal.";
    return out.str();
}
static LRESULT CALLBACK setupProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(w) == 140) {
            auto report = setupReport();
            SetWindowTextW(setupText, report.c_str());
            auto position = report.find(L"SAVED-SETTINGS CHECK");
            if (position != std::wstring::npos) {
                SendMessageW(setupText, EM_SETSEL, position, position);
                auto line = SendMessageW(setupText, EM_LINEFROMCHAR, position, 0);
                auto first = SendMessageW(setupText, EM_GETFIRSTVISIBLELINE, 0, 0);
                SendMessageW(setupText, EM_LINESCROLL, 0, line - first);
            }
        } else if (LOWORD(w) == 141)
            ShellExecuteW(hwnd, L"open", L"notepad.exe",
                          quote((appDirectory / L"INSTALLATION-AND-USE.txt").wstring()).c_str(),
                          nullptr, SW_SHOWNORMAL);
        else if (LOWORD(w) == 142)
            DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        setupWindow = nullptr;
        EnableWindow(window, TRUE);
        SetForegroundWindow(window);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
static void showSetup() {
    if (setupWindow) {
        SetForegroundWindow(setupWindow);
        return;
    }
    WNDCLASSW c{};
    c.lpfnWndProc = setupProcedure;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"XPFSR24SimulatorSetup";
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassW(&c);
    setupWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName,
                                  L"Simulator setup - required settings and read-only check",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 830, 720, window, nullptr, c.hInstance, nullptr);
    ensure(setupWindow != nullptr, "Cannot open the simulator setup guide");
    setupText = control(L"EDIT", setupReport().c_str(), ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20,
                        20, 775, 585, 139, setupWindow);
    control(L"BUTTON", L"Check saved settings", BS_PUSHBUTTON | WS_TABSTOP, 20, 625, 245, 34, 140,
            setupWindow);
    control(L"BUTTON", L"Open full guide", BS_PUSHBUTTON | WS_TABSTOP, 285, 625, 245, 34, 141,
            setupWindow);
    control(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 550, 625, 245, 34, 142, setupWindow);
    EnableWindow(window, FALSE);
    ShowWindow(setupWindow, SW_SHOW);
    SetFocus(setupText);
    MSG msg{};
    while (setupWindow && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(setupWindow, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
