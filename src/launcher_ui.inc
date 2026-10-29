// SPDX-License-Identifier: MIT
static HWND control(const wchar_t *type, const wchar_t *label, DWORD style, int x, int y, int w,
                    int h, int id = 0, HWND parent = nullptr) {
    auto c = CreateWindowExW(
        0, type, label,
        WS_CHILD | WS_VISIBLE | style |
            (std::wstring(type) == L"COMBOBOX" ? CBS_OWNERDRAWFIXED | CBS_HASSTRINGS : 0) |
            (std::wstring(type) == L"STATIC" ? SS_NOPREFIX : 0),
        x, y, w, h, parent ? parent : window, reinterpret_cast<HMENU>(INT_PTR(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    theme::apply(c, type);
    return c;
}
static HWND scaledControl(const wchar_t *type, const wchar_t *label, DWORD style, int x, int y,
                          int w, int h, int id = 0, HWND parent = nullptr) {
    return control(type, label, style, theme::px(x), theme::px(y), theme::px(w), theme::px(h), id,
                   parent);
}
static SIZE modalSize(int width, int height) {
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return {std::min(theme::px(width), int(work.right - work.left) - 32),
            std::min(theme::px(height), int(work.bottom - work.top) - 32)};
}
static void error(const std::exception &e) {
    MessageBoxW(window, wide(e.what()).c_str(), L"FSR 3/4 + Frame Gen for X-Plane",
                MB_OK | MB_ICONERROR);
}
struct SettingHelp {
    int id;
    const wchar_t *title, *body;
};
static constexpr SettingHelp settingHelp[] = {
    {200, L"X-Plane folder",
     L"Choose the folder containing X-Plane.exe. The launcher installs its camera plugin there and "
     L"enables its Vulkan layer only for the simulator it starts.\n\nBest use: select your X-Plane "
     L"12 installation once; this choice is remembered."},
    {201, L"Optional launch arguments",
     L"Extra X-Plane command-line arguments are passed to the simulator exactly as "
     L"entered.\n\nBest use: leave blank unless you already use a specific simulator startup "
     L"option. Your text is saved automatically."},
    {202, L"Resolution and FSR presets",
     L"A preset sets fullscreen OUTPUT resolution, internal rendering scale, the FSR provider and "
     L"AA mode. MSAA, FXAA and frame generation are set Off. It applies immediately while X-Plane "
     L"is closed, backs up changes and is remembered for the next launch.\n\nBest use: Native AA "
     L"for sharper edges at full resolution; Quality for higher performance. 4K Quality renders at "
     L"1440p and outputs 4K. Only resolutions your primary display advertises appear. FSR 4 uses "
     L"the experimental 4.1.2 provider; hardware/driver support is checked by the runtime. Run the "
     L"AA check before flight."},
    {203, L"FSR reconstruction mode",
     L"Native AA reduces jagged edges at full internal resolution. Upscaling reconstructs a larger "
     L"output from a smaller input. FSR 3.1.6 is the compatibility provider; FSR 4.1.2 is the ML "
     L"preview. Diagnostics loads the layer without FSR reconstruction.\n\nBest use: choose a "
     L"preset first. Changing this mode switches to Custom; match internal scale using the "
     L"main-screen "
     L"graphics controls. MSAA is experimental with FSR; FXAA must be Off. Changing modes requires "
     L"a simulator "
     L"restart."},
    {204, L"Frame generation",
     L"Inserts interpolated frames between rendered frames. It can improve displayed smoothness "
     L"but does not repair low base FPS, stutter or input latency. Gauges, props and UI may show "
     L"artifacts. 3.1.6 is the compatibility option; 4.0.2 is the ML preview.\n\nBest use: enable "
     L"only after real rendered frame times are stable. Cockpit screen restore must be Off; "
     L"selecting FG turns that restore Off automatically. Changing provider requires restart; Off "
     L"can be selected during flight."},
    {205, L"Enable FSR processing",
     L"Enables or bypasses the selected FSR mode in the simulator launched here. This checkbox can "
     L"be changed during flight. It does not remove the loaded Vulkan layer.\n\nBest use: compare "
     L"native rendering with FSR. The live status explicitly shows paused processing when "
     L"unchecked."},
    {206, L"FSR 3 history tuning",
     L"Uses balanced history rejection for FSR 3.1.6. This can reduce trails but can "
     L"increase shimmer. It has no effect on the FSR 4 provider.\n\nBest use: try it for trails in "
     L"compatibility mode. It cannot provide complete motion for clouds, moving aircraft or "
     L"propellers. Restart to apply."},
    {207, L"FSR Ghosting Fix - cockpit screens",
     L"Experimental compositor for the default Cirrus SR22 PFD and MFD. It restores current-scene "
     L"screen pixels after FSR, with depth and view-stability checks. It does not extract a "
     L"native-resolution avionics texture or fix scenery/cloud ghosting.\n\nBest use: test the "
     L"default SR22 with FG Off. Check alignment with the green tint first; disable restore if it "
     L"is misplaced or flickers. Enabling restore turns FG Off. Restart to apply."},
    {208, L"Screen-alignment tint",
     L"Adds green to accepted cockpit-screen pixels so you can check that restoration stays inside "
     L"the default SR22 PFD/MFD. No green can mean geometry, motion or depth checks rejected the "
     L"frame.\n\nBest use: a brief alignment test, then turn Off and restart. This enables cockpit "
     L"restore and turns FG Off. Dispatch counts alone do not establish correct screen coverage."},
    {209, L"Live injection status",
     L"NOT RUNNING: no simulator is monitored by this launcher.\nSTARTING: the simulator is "
     L"running, but layer activity is not confirmed.\nLAYER LOADED: the Vulkan layer reports "
     L"activity, but FSR is waiting or bypassed.\nFSR ACTIVE: recent FSR command recordings and "
     L"Vulkan presentation have been reported.\nPAUSED or NO RECENT ACTIVITY: processing is "
     L"disabled or telemetry has stopped progressing. GROUND CAMERA SAFETY: when the aircraft is "
     L"on the ground, an external camera at or below 1.5 metres AGL uses native rendering and "
     L"pauses FG. Raise it to 2.5 metres, enter the cockpit or take off to resume. "
     L"Camera/controller settings are unchanged.\n\nBest use: launch from this app, load a "
     L"flight, then check for green FSR ACTIVE. A one-time Windows notification is requested when "
     L"FSR first becomes active; Windows may suppress it. This is not a visual-quality or FPS "
     L"guarantee."},
    {210, L"MSAA",
     L"Multisample antialiasing smooths geometry edges in X-Plane's native renderer. Higher levels "
     L"cost GPU time and memory. The layer now resolves multisample depth for FSR 3/4 and FG. "
     L"This combination is experimental and needs live-flight checks.\n\nBest use: start Off, then "
     L"try 2x if geometry edges still shimmer. It cannot fix cloud or texture aliasing. "
     L"Apply graphics with X-Plane closed; FSR and FG selections are preserved."},
    {211, L"FXAA",
     L"A lightweight post-process that smooths contrast edges but can soften cockpit text.\n\nBest "
     L"use: native rendering when GPU performance is limited. Leave Off with temporal FSR; presets "
     L"do this automatically."},
    {212, L"Internal rendering scale",
     L"Controls the resolution X-Plane renders before reconstruction. Full is required for Native "
     L"AA. Quality uses a 1.5x output/input ratio; Performance uses 2x. Lower input resolutions "
     L"improve performance but lose fine screen detail.\n\nBest use: Quality for a first upscaling "
     L"test. Set output resolution separately: selecting 1440p output does not produce 4K."},
    {213, L"Output resolution",
     L"The final fullscreen resolution shown by X-Plane. Keep current leaves window/output "
     L"settings untouched. The launcher changes only supported single-monitor preferences and does "
     L"not change Windows desktop modes or drivers.\n\nBest use: 3840 x 2160 output with Quality "
     L"scale for 2560 x 1440 input to 4K. Your primary display must advertise the output mode."},
    {214, L"Motion source - experimental comparison",
     L"Camera-derived is the existing default. Native renderer motion reads scene SSR depth, "
     L"own-aircraft flags and reprojection matrices at the rendering pass, with GPU projection "
     L"history for zoom. This is an opt-in experiment.\n\n"
     L"Best use: an A/B flight test with MSAA Off, FXAA Off, frame generation Off and "
     L"cockpit-screen fix Off. Restart X-Plane after changing this choice. Both FSR 3 and FSR 4 "
     L"can be tested. The current build requires a supported scene SSR shader and matching depth "
     L"in the same primary command buffer. If unavailable, native rendering stays active and "
     L"status explains the bypass. It never switches to camera-derived motion silently.\n\n"
     L"It does not add exact motion for independently moving traffic, props, clouds, rain or "
     L"glass. This experiment has not been validated in a live flight. Return to Camera-derived to "
     L"use the existing MSAA/FG/screen-fix features."}};
static const SettingHelp *helpById(int id) {
    for (auto &h : settingHelp)
        if (h.id == id)
            return &h;
    return nullptr;
}
static void helpIcon(HWND parent, int id, int x, int y) {
    auto button = control(L"BUTTON", L"?", BS_PUSHBUTTON | WS_TABSTOP, x, y, 25, 25, id, parent);
    auto *h = helpById(id);
    theme::name(button, (std::wstring(L"Help: ") + h->title).c_str());
    auto tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0,
                               0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = parent;
    tool.uId = reinterpret_cast<UINT_PTR>(button);
    tool.lpszText = const_cast<wchar_t *>(h->body);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 480);
    SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
}
#include "launcher_help.inc"
static bool showHelp(HWND parent, int id) {
    auto *h = helpById(id);
    if (!h)
        return false;
    modernHelp(parent, h->title, h->body);
    return true;
}
static void collectChoices() {
    preferences.simulator = text(pathBox);
    preferences.arguments = text(argsBox);
    preferences.mode = int(SendMessageW(modeBox, CB_GETCURSEL, 0, 0));
    preferences.frameGeneration = int(SendMessageW(fgBox, CB_GETCURSEL, 0, 0));
    preferences.motionSource = int(SendMessageW(motionBox, CB_GETCURSEL, 0, 0));
    preferences.enabled = SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.ghostReduction = SendMessageW(ghostBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.cockpitRestore = SendMessageW(cockpitBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.alignmentTint = SendMessageW(tintBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
static const wchar_t *motionSetupText() {
    return preferences.motionSource == 1
               ? L"Native motion: MSAA, FXAA, FG and cockpit-screen fix Off. Restart "
                 L"X-Plane.\r\nMissing SSR inputs keep native rendering; check live status."
               : L"Camera motion: FXAA Off; MSAA optional (experimental). Native AA = "
                 L"Full.\r\nUpscaling = below Full. Choose a preset with X-Plane closed; see Sim "
                 L"setup.";
}
static void saveChoices() {
    if (!controlsReady)
        return;
    collectChoices();
    if (motionSetupNote)
        SetWindowTextW(motionSetupNote, designSetupHint());
    try {
        launcher::save(appDirectory / L"settings.ini", preferences);
        SetWindowTextW(savedLabel, L"Choices saved automatically");
        theme::tone(savedLabel, theme::green);
    } catch (...) {
        SetWindowTextW(savedLabel, L"Choices NOT saved - check folder permissions");
        theme::tone(savedLabel, theme::red);
        throw;
    }
}
static void runtimeBanner(launcher::RuntimeState state, const app::Telemetry &status) {
    const wchar_t *message = L"NOT RUNNING\r\nLaunch from this app";
    switch (state) {
    case launcher::RuntimeState::native:
        message = L"NATIVE LAUNCH\r\nFSR not requested";
        break;
    case launcher::RuntimeState::starting:
        message = L"STARTING / WAITING\r\nAwaiting layer proof";
        break;
    case launcher::RuntimeState::loaded:
        message = L"VULKAN LAYER LOADED\r\nFSR waiting / bypassed";
        break;
    case launcher::RuntimeState::active:
        message = L"FSR ACTIVE\r\nVulkan layer confirmed";
        break;
    case launcher::RuntimeState::groundSafety:
        message = L"GROUND CAMERA SAFETY\r\nNative rendering";
        break;
    case launcher::RuntimeState::paused:
        message = L"FSR PAUSED\r\nProcessing disabled";
        break;
    case launcher::RuntimeState::stalled:
        message = L"NO RECENT ACTIVITY\r\nWaiting for updates";
        break;
    default:
        break;
    }
    bannerState = state;
    SetWindowTextW(injectionBanner, message);
    InvalidateRect(injectionBanner, nullptr, FALSE);
    if (footerStatus)
        SetWindowTextW(footerStatus, message);
    auto tone =
        state == launcher::RuntimeState::active ? theme::green
        : state == launcher::RuntimeState::groundSafety || state == launcher::RuntimeState::paused
            ? theme::gold
            : theme::muted;
    theme::tone(injectionBanner, tone);
    if (footerStatus)
        theme::tone(footerStatus, tone);
    SetWindowTextW(window, state == launcher::RuntimeState::active
                               ? L"FSR ACTIVE - FSR 3/4 + Frame Gen for X-Plane"
                               : L"FSR 3/4 + Frame Gen for X-Plane - Native Motion EXPERIMENTAL");
    if (state == launcher::RuntimeState::active && !activeNotificationSent) {
        NOTIFYICONDATAW info{sizeof(info)};
        info.hWnd = window;
        info.uID = 1;
        info.uFlags = NIF_INFO;
        wcscpy_s(info.szInfoTitle, L"FSR is active in X-Plane");
        auto detail = L"Vulkan layer confirmed. FSR " + wide(status.provider) + L"; " +
                      std::to_wstring(status.outputWidth) + L" x " +
                      std::to_wstring(status.outputHeight) +
                      L" output. Open the launcher for live status.";
        wcsncpy_s(info.szInfo, detail.c_str(), _TRUNCATE);
        info.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &info);
        activeNotificationSent = true;
    }
}
#include "graphics_ui.inc"
#include "setup_ui.inc"
#include "update_ui.inc"
#include "launcher_design.inc"
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE:
        createMainControls(hwnd);
        return 0;
    case WM_PAINT:
        designPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        designLayout(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        designWheel(hwnd, GET_WHEEL_DELTA_WPARAM(w));
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (LOWORD(w)) {
        case SB_THUMBTRACK:
            designScroll = si.nTrackPos;
            break;
        case SB_LINEUP:
            designScroll -= 36;
            break;
        case SB_LINEDOWN:
            designScroll += 36;
            break;
        case SB_PAGEUP:
            designScroll -= designPage;
            break;
        case SB_PAGEDOWN:
            designScroll += designPage;
            break;
        case SB_TOP:
            designScroll = 0;
            break;
        case SB_BOTTOM:
            designScroll = designBottom;
            break;
        }
        designLayout(hwnd);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(l);
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        info->ptMinTrackSize = {std::min(theme::px(840), int(work.right - work.left) - 16),
                                std::min(theme::px(600), int(work.bottom - work.top) - 16)};
        return 0;
    }
    case WM_DPICHANGED: {
        theme::dpiChanged(hwnd, HIWORD(w));
        font = theme::fonts[0];
        titleFont = theme::fonts[1];
        auto r = reinterpret_cast<RECT *>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        designLayout(hwnd);
        return 0;
    }
    case WM_DRAWITEM:
        if (theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT *>(l)))
            return TRUE;
        break;
    case WM_MEASUREITEM:
        if (auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(l); item->CtlType == ODT_COMBOBOX) {
            item->itemHeight = theme::px(30);
            return TRUE;
        }
        break;
    case WM_APP + 6:
        designFocus(hwnd, reinterpret_cast<HWND>(l));
        return 0;
    case WM_COMMAND:
        if (!controlsReady)
            return 0;
        try {
            int id = LOWORD(w), event = HIWORD(w);
            if (showHelp(hwnd, id))
                return 0;
            if ((id == 10 || id == 12) && event == EN_CHANGE) {
                SetWindowTextW(savedLabel, L"Saving choices...");
                SetTimer(hwnd, 2, 500, nullptr);
                return 0;
            }
            if (id == 109 &&
                (event == CBN_SELENDOK ||
                 (event == CBN_SELCHANGE && !SendMessageW(presetBox, CB_GETDROPPEDSTATE, 0, 0)))) {
                try {
                    selectResolutionPreset();
                } catch (...) {
                    syncPresetSelection();
                    throw;
                }
                return 0;
            }
            if (((id == 110 || id == 112 || id == 113) && event == CBN_SELCHANGE) ||
                (id == 111 && event == BN_CLICKED)) {
                editGraphics();
                return 0;
            }
            switch (id) {
            case 250:
                platformHelp(hwnd);
                break;
            case 106:
                applyCustomGraphics();
                break;
            case 11: {
                BROWSEINFOW info{};
                info.hwndOwner = window;
                info.lpszTitle = L"Choose the folder containing X-Plane.exe";
                info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                auto item = SHBrowseForFolderW(&info);
                if (item) {
                    wchar_t selected[MAX_PATH]{};
                    if (SHGetPathFromIDListW(item, selected))
                        SetWindowTextW(pathBox, selected);
                    CoTaskMemFree(item);
                    saveChoices();
                }
                break;
            }
            case 20:
                ensure(!aaChild, "Wait for the GPU check to finish");
                saveChoices();
                applyRememberedGraphics(text(pathBox));
                launch(text(pathBox), text(argsBox), false);
                break;
            case 21:
                saveChoices();
                applyRememberedGraphics(text(pathBox));
                launch(text(pathBox), text(argsBox), true);
                break;
            case 22: {
                bool enabled = SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (shared)
                    InterlockedExchange(&shared->enabled, enabled);
                fs::create_directories(appDirectory / L"logs");
                std::ofstream(appDirectory / L"logs/renderer-control.txt") << (enabled ? '1' : '0');
                saveChoices();
                break;
            }
            case 40:
                if (event == CBN_SELCHANGE) {
                    editGraphics();
                }
                break;
            case 41:
                if (event == CBN_SELCHANGE) {
                    int selected = int(SendMessageW(fgBox, CB_GETCURSEL, 0, 0));
                    if (running() && selected && selected != launchedFG) {
                        SendMessageW(fgBox, CB_SETCURSEL, lastFGSelection, 0);
                        throw std::runtime_error(
                            "Restart X-Plane to change the frame generation provider");
                    }
                    if (selected && !running()) {
                        SendMessageW(cockpitBox, BM_SETCHECK, BST_UNCHECKED, 0);
                        SendMessageW(tintBox, BM_SETCHECK, BST_UNCHECKED, 0);
                    }
                    if (running())
                        std::ofstream(appDirectory / L"logs/renderer-fg-control.txt")
                            << (selected ? '1' : '0');
                    lastFGSelection = selected;
                    saveChoices();
                    if (!running())
                        graphicsMessage(L"Frame generation choice saved. The selected FSR mode is "
                                        L"retained. Restart X-Plane to apply the provider.",
                                        L"Frame generation choice saved · Restart to apply");
                }
                break;
            case 45:
                if (event == CBN_SELCHANGE) {
                    saveChoices();
                    if (running())
                        SetWindowTextW(statusBox,
                                       L"Motion choice saved; restart X-Plane to apply it.");
                }
                break;
            case 42:
                saveChoices();
                break;
            case 43:
            case 44:
                if ((id == 43 && SendMessageW(cockpitBox, BM_GETCHECK, 0, 0) == BST_CHECKED) ||
                    (id == 44 && SendMessageW(tintBox, BM_GETCHECK, 0, 0) == BST_CHECKED)) {
                    SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
                    SendMessageW(cockpitBox, BM_SETCHECK, BST_CHECKED, 0);
                } else if (id == 43)
                    SendMessageW(tintBox, BM_SETCHECK, BST_UNCHECKED, 0);
                saveChoices();
                break;
            case 31:
                ShellExecuteW(hwnd, L"open", L"notepad.exe",
                              quote((appDirectory / L"logs/renderer.log").wstring()).c_str(),
                              nullptr, SW_SHOWNORMAL);
                break;
            case 32:
                removePlugin(text(pathBox));
                SetWindowTextW(statusBox, L"This package's camera plugin has been removed.");
                break;
            case 33:
                showSetup();
                break;
            case 34:
                checkAA();
                break;
            case 36:
                checkFG();
                break;
            case 37:
                checkCockpit();
                break;
            case 38:
                ShellExecuteW(hwnd, L"open",
                              (appDirectory / L"cockpit-check/comparison.html").c_str(), nullptr,
                              nullptr, SW_SHOWNORMAL);
                break;
            case 39:
                showUpdates();
                break;
            case 107:
                restoreGraphics();
                break;
            }
            refresh();
        } catch (const std::exception &e) {
            error(e);
        }
        return 0;
    case WM_TIMER:
        if (w == 3) {
            KillTimer(hwnd, 3);
            if (autoCheckUpdates)
                startUpdateCheck();
        } else if (w == 4) {
            pollUpdate();
        } else if (w == 2) {
            KillTimer(hwnd, 2);
            try {
                saveChoices();
            } catch (const std::exception &e) {
                error(e);
            }
        } else
            refresh();
        return 0;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return theme::colors(message, w, l);
    case WM_APP + 1:
        if (l == WM_LBUTTONDBLCLK || l == NIN_BALLOONUSERCLICK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        updateJob->cancel = true;
        try {
            saveChoices();
        } catch (const std::exception &e) {
            error(e);
            return 0;
        }
        saveWindowSize(hwnd);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        detach();
        NOTIFYICONDATAW tray{sizeof(tray)};
        tray.hWnd = hwnd;
        tray.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &tray);
        if (child)
            CloseHandle(child);
        if (aaChild)
            CloseHandle(aaChild);
        theme::shutdown();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, w, l);
}
