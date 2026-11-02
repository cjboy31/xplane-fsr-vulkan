// SPDX-License-Identifier: MIT
// Shared launcher layout. Native controls retain keyboard and accessibility behavior.
static int designScroll{}, designBottom{}, designPage{};
static bool designLayoutBusy{};
static theme::Layout designMoves;
static int designWheelRemainder{};
static HWND designCanvas{};
static HWND designControl(int id) {
    auto h = GetDlgItem(window, id);
    return h ? h : GetDlgItem(designCanvas, id);
}
static void designPaint(HWND, bool = false);
static LRESULT CALLBACK designCanvasProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        designPaint(h, true);
        return 0;
    }
    if (m == WM_ERASEBKGND)
        return 1;
    if (m == WM_COMMAND || m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_CTLCOLORSTATIC ||
        m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN || m == WM_CTLCOLORLISTBOX ||
        m == WM_MOUSEWHEEL)
        return SendMessageW(GetParent(h), m, w, l);
    return DefWindowProcW(h, m, w, l);
}
static std::vector<RECT> designCards;
static HWND designHero{}, designSubtitle{}, designVersion{}, designPlatform{};
static HWND designHeads[7]{}, designLabels[10]{};
static bool designHasMotion() {
#ifdef XPFSR_NATIVE_MOTION
    return true;
#else
    return false;
#endif
}
static const wchar_t *designSetupHint() {
#ifdef XPFSR_NATIVE_MOTION
    return motionSetupText();
#else
    return L"Native AA: Full input. Upscaling: below Full. FXAA Off. Start with FG Off; see Sim "
           L"setup.";
#endif
}
static void designPlace(HWND h, int x, int y, int width, int height, bool fixed = false) {
    if (!h)
        return;
    int top = theme::px(y - (fixed ? 0 : designScroll));
    designMoves.place(h, theme::px(x), top, theme::px(width), theme::px(height));
}
static void designHelp(int id, int x, int y) { designPlace(designControl(id), x, y, 24, 24); }
static void designLayout(HWND hwnd) {
    if (designLayoutBusy || !controlsReady)
        return;
    designLayoutBusy = true;
    RECT client;
    GetClientRect(hwnd, &client);
    int width = MulDiv(client.right, 96, theme::dpi),
        height = MulDiv(client.bottom, 96, theme::dpi);
    int area = std::min(width, 1360), origin = (width - area) / 2;
    bool wide = area >= 1100;
    int left = origin + 32, leftWidth = wide ? area - 456 : area - 64,
        right = wide ? left + leftWidth + 24 : left;
    int rightWidth = wide ? 368 : leftWidth;
    const int profileY = 144, profileH = 152, engineY = 316,
              engineH = designHasMotion() ? 292 : 220;
    int qualityY = engineY + engineH + 20, qualityH = 292, temporalY = qualityY + qualityH + 20;
    int leftBottom = temporalY + 148;
    int statusY = wide ? 144 : leftBottom + 76, connectionY = statusY + 282,
        diagnosticY = connectionY + 254;
    designBottom = std::max(leftBottom + (designHasMotion() ? 68 : 40), diagnosticY + 258);
    designPage = std::max(1, height - 88);
    designMoves.place(designCanvas, 0, 0, client.right, theme::px(designPage));
    designScroll = std::clamp(designScroll, 0, std::max(0, designBottom - designPage));
    SCROLLINFO si{sizeof(si),
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  designBottom - 1,
                  UINT(designPage),
                  designScroll,
                  0};
    SetScrollInfo(hwnd, SB_VERT, &si, FALSE);
    designCards.clear();
    auto card = [&](int x, int y, int w, int h) {
        designCards.push_back({theme::px(x), theme::px(y - designScroll), theme::px(x + w),
                               theme::px(y + h - designScroll)});
    };
    card(left, profileY, leftWidth, profileH);
    card(left, engineY, leftWidth, engineH);
    card(left, qualityY, leftWidth, qualityH);
    card(left, temporalY, leftWidth, 148);
    card(right, statusY, rightWidth, 274);
    card(right, connectionY, rightWidth, 246);
    card(right, diagnosticY, rightWidth, 238);
    theme::face(designHero, area < 1100 ? 5 : 1);
    designPlace(designHero, left, 30, area - 360, 47);
    designPlace(designSubtitle, left, 86, area - 360, 26);
    designPlace(designVersion, left, 115, area - 360, 20);
    designPlace(designControl(33), origin + area - 324, 36, 136, 36);
    designPlace(updatesButton, origin + area - 172, 36, 140, 36);
    designPlace(designPlatform, origin + area - 324, 80, 292, 30);
    int ys[] = {profileY, engineY, qualityY, temporalY, statusY, connectionY, diagnosticY};
    for (int i = 0; i < 7; ++i)
        designPlace(designHeads[i], i < 4 ? left + 24 : right + 24, ys[i] + 20,
                    i < 4 ? leftWidth - 48 : rightWidth - 84, 26);
    int half = (leftWidth - 64) / 2, lx = left + 24, rx = lx + half + 16;
    auto fieldLabel = [&](HWND h, int x, int y, int w) { designPlace(h, x, y, w, 21); };
    fieldLabel(designLabels[0], lx, profileY + 57, leftWidth - 88);
    designHelp(202, left + leftWidth - 48, profileY + 51);
    designPlace(presetBox, lx, profileY + 84, leftWidth - 48, 360);
    designPlace(presetNote, lx, profileY + 118, leftWidth - 48, 26);
    fieldLabel(designLabels[1], lx, engineY + 56, half - 32);
    fieldLabel(designLabels[2], rx, engineY + 56, half - 32);
    designPlace(modeBox, lx, engineY + 84, half, 200);
    designHelp(203, lx + half - 24, engineY + 51);
    designPlace(fgBox, rx, engineY + 84, half, 180);
    designHelp(204, rx + half - 24, engineY + 51);
#ifdef XPFSR_NATIVE_MOTION
    fieldLabel(designLabels[3], lx, engineY + 138, leftWidth - 88);
    designPlace(motionBox, lx, engineY + 166, leftWidth - 48, 150);
    designHelp(214, left + leftWidth - 48, engineY + 133);
#endif
    const int nrY=engineY+(designHasMotion()?210:138);
    fieldLabel(designLabels[9],lx,nrY,leftWidth-88);designHelp(215,left+leftWidth-48,nrY-5);
    designPlace(neuralBox,lx,nrY+28,half,180);
    designPlace(designControl(47),rx,nrY+25,half,36);
    fieldLabel(designLabels[4], lx, qualityY + 56, half - 32);
    designHelp(210, lx + half - 24, qualityY + 51);
    fieldLabel(designLabels[5], rx, qualityY + 56, half - 32);
    designHelp(212, rx + half - 24, qualityY + 51);
    designPlace(msaaBox, lx, qualityY + 84, half, 180);
    designPlace(scaleBox, rx, qualityY + 84, half, 210);
    fieldLabel(designLabels[6], lx, qualityY + 133, half - 32);
    designHelp(213, lx + half - 24, qualityY + 128);
    designPlace(outputBox, lx, qualityY + 161, half, 220);
    designPlace(fxaaBox, rx, qualityY + 161, half - 32, 32);
    designHelp(211, rx + half - 24, qualityY + 164);
    designPlace(designControl(107), lx, qualityY + 197, half, 34);
    designPlace(designControl(106), rx, qualityY + 197, half, 34);
    designPlace(graphicsStatus, lx, qualityY + 234, leftWidth - 48, 18);
    designPlace(keepUIBox,lx,qualityY+259,leftWidth-88,28);
    designHelp(216,left+leftWidth-48,qualityY+258);
    designPlace(toggle, lx, temporalY + 59, half - 32, 30);
    designHelp(205, lx + half - 24, temporalY + 61);
    designPlace(cockpitBox, rx, temporalY + 59, half - 32, 30);
    designHelp(207, rx + half - 24, temporalY + 61);
    designPlace(ghostBox, lx, temporalY + 101, half - 32, 30);
    designHelp(206, lx + half - 24, temporalY + 103);
    designPlace(tintBox, rx, temporalY + 101, half - 32, 30);
    designHelp(208, rx + half - 24, temporalY + 103);
    designPlace(motionSetupNote, left + 4, leftBottom + 12, leftWidth - 8,
                designHasMotion() ? 48 : 26);
    designHelp(209, right + rightWidth - 48, statusY + 21);
    designPlace(injectionBanner, right + 24, statusY + 60, rightWidth - 48, 48);
    designPlace(statusBox, right + 24, statusY + 124, rightWidth - 48, 128);
    fieldLabel(designLabels[7], right + 24, connectionY + 56, rightWidth - 88);
    designHelp(200, right + rightWidth - 48, connectionY + 51);
    designPlace(pathBox, right + 24, connectionY + 85, rightWidth - 140, 32);
    designPlace(designControl(11), right + rightWidth - 104, connectionY + 82, 80, 36);
    fieldLabel(designLabels[8], right + 24, connectionY + 139, rightWidth - 88);
    designHelp(201, right + rightWidth - 48, connectionY + 134);
    designPlace(argsBox, right + 24, connectionY + 168, rightWidth - 48, 32);
    int buttonWidth = (rightWidth - 64) / 3;
    for (int i = 0; i < 3; ++i)
        designPlace(designControl(i == 0   ? 34
                                  : i == 1 ? 36
                                           : 37),
                    right + 24 + i * (buttonWidth + 8), diagnosticY + 63, buttonWidth, 34);
    int toolWidth = (rightWidth - 56) / 2;
    designPlace(designControl(38), right + 24, diagnosticY + 111, toolWidth, 34);
    designPlace(designControl(31), right + 32 + toolWidth, diagnosticY + 111, toolWidth, 34);
    designPlace(designControl(32), right + 24, diagnosticY + 159, rightWidth - 48, 32);
    designPlace(designControl(540), right + 24, diagnosticY + 205, rightWidth - 48, 21);
    int launchX = origin + area - 262, nativeX = launchX - 226;
    designPlace(designControl(20), launchX, height - 66, 230, 44, true);
    designPlace(designControl(21), nativeX, height - 66, 210, 44, true);
    designPlace(footerStatus, left, height - 75, std::max(240, nativeX - left - 20), 42, true);
    designPlace(savedLabel, left, height - 29, std::max(240, nativeX - left - 20), 21, true);
    designMoves.commit(hwnd);
    designLayoutBusy = false;
}
static void designPaint(HWND h, bool canvas) {
    PAINTSTRUCT paint;
    auto target = BeginPaint(h, &paint);
    RECT r;
    GetClientRect(h, &r);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1L, r.right), std::max(1L, r.bottom));
    auto old = SelectObject(dc, bitmap);
    FillRect(dc, &r, theme::backgroundBrush);
    if (canvas) {
        for (auto &c : designCards)
            theme::rounded(dc, c, theme::card, theme::border, 14);
    } else {
        int bottom = r.bottom - theme::px(88);
        HPEN pen = CreatePen(PS_SOLID, 1, theme::border);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, 0, bottom, nullptr);
        LineTo(dc, r.right, bottom);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(h, &paint);
}
static bool designScrollTo(HWND h, int next) {
    next = std::clamp(next, 0, std::max(0, designBottom - designPage));
    if (next == designScroll)
        return false;
    designScroll = next;
    designLayout(h);
    return true;
}
static void designWheel(HWND h, int delta) {
    designWheelRemainder += delta;
    const int steps = designWheelRemainder / WHEEL_DELTA;
    designWheelRemainder %= WHEEL_DELTA;
    if (!steps)
        return;
    designScrollTo(h, designScroll - steps * 72);
}
static void designFocus(HWND h, HWND control) {
    int id = GetDlgCtrlID(control);
    if (id == 20 || id == 21 || control == savedLabel || control == footerStatus)
        return;
    if (GetParent(control) != designCanvas)
        return;
    RECT r;
    GetWindowRect(control, &r);
    MapWindowPoints(HWND_DESKTOP, h, reinterpret_cast<POINT *>(&r), 2);
    int top = MulDiv(r.top, 96, theme::dpi), bottom = MulDiv(r.bottom, 96, theme::dpi);
    if (top < 12)
        designScrollTo(h, designScroll + top - 12);
    else if (bottom > designPage - 12)
        designScrollTo(h, designScroll + bottom - designPage + 12);
    else
        return;
}
static void createMainControls(HWND hwnd) {
    window = hwnd;
    lastSubmittedChoices=preferences;submittedChoicesReady=true;
    theme::initialize(hwnd);
    font = theme::fonts[0];
    titleFont = theme::fonts[1];
    auto make = [&](const wchar_t *type, const wchar_t *value, DWORD style, int id) {
        return control(type, value, style, 0, 0, 320, 34, id);
    };
    auto small = [&](HWND h, COLORREF color = theme::muted) {
        theme::face(h, 2);
        theme::tone(h, color);
    };
    designHero = make(L"STATIC", L"FSR 3 / 4 + Frame Gen", 0, 500);
    theme::face(designHero, 1);
    designSubtitle =
        make(L"STATIC", L"Upscaling, antialiasing & frame generation for X-Plane 12", 0, 501);
    theme::tone(designSubtitle, theme::muted);
    auto version = std::wstring(updates::channelName(updates::installedChannel())) +
                   L" build  /  " + wide(updates::currentVersion);
    designVersion = make(L"STATIC", version.c_str(), 0, 502);
    small(designVersion, theme::gold);
    for (auto h : {designHero, designSubtitle, designVersion})
        theme::surface(h, 1);
    make(L"BUTTON", L"Sim setup", BS_PUSHBUTTON | WS_TABSTOP, 33);
    updatesButton = make(L"BUTTON", L"Updates", BS_PUSHBUTTON | WS_TABSTOP, 39);
    designPlatform =
        make(L"BUTTON", L"Windows / Vulkan  ·  Platform support", BS_PUSHBUTTON | WS_TABSTOP, 250);
    const wchar_t *heads[] = {L"Flight profile",
                              L"Rendering",
                              L"Image quality",
                              L"Temporal options",
                              L"Live simulator status",
                              L"Simulator connection",
                              L"Diagnostics"};
    for (int i = 0; i < 7; ++i) {
        designHeads[i] = make(L"STATIC", heads[i], 0, 510 + i);
        theme::face(designHeads[i], 3);
    }
    const wchar_t *labels[] = {
        L"RESOLUTION PRESET", L"FSR MODE",       L"FRAME GENERATION",
        L"MOTION SOURCE",     L"MSAA",           L"INPUT SCALE",
        L"OUTPUT RESOLUTION", L"X-PLANE FOLDER", L"OPTIONAL LAUNCH ARGUMENTS",L"AMD NEURAL RENDERING - EXPERIMENTAL"};
    for (int i = 0; i < 10; ++i) {
        designLabels[i] = make(L"STATIC", labels[i], 0, i==9?543:520+i);
        small(designLabels[i]);
    }
    if (!designHasMotion())
        ShowWindow(designLabels[3], SW_HIDE);
    pathBox = make(L"EDIT", preferences.simulator.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 10);
    argsBox = make(L"EDIT", preferences.arguments.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 12);
    make(L"BUTTON", L"Browse", BS_PUSHBUTTON | WS_TABSTOP, 11);
    auto combo = [&](int id) {
        return make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, id);
    };
    presetBox = combo(109);
    populatePresets();
    presetNote = make(L"STATIC", L"", 0, 529);
    small(presetNote);
    updatePresetSummary();
    modeBox = combo(40);
    for (auto *label :
         {L"Diagnostics only · FSR Off", L"FSR 3.1.6 · Native AA", L"FSR 3.1.6 · Upscaling",
          L"FSR 4.1.2 · Native AA (ML)", L"FSR 4.1.2 · Upscaling (ML)"})
        SendMessageW(modeBox, CB_ADDSTRING, 0, LPARAM(label));
    SendMessageW(modeBox, CB_SETCURSEL, preferences.mode, 0);
    fgBox = combo(41);
    neuralBox=combo(46);
    for(auto label:{L"Off - Live bypass",L"Subtle - 25% - Live",L"Balanced - 50% - Live",L"Strong - 100% - Live",L"Custom - Live NR settings"})
        SendMessageW(neuralBox,CB_ADDSTRING,0,LPARAM(label));
    SendMessageW(neuralBox,CB_SETCURSEL,app::effectiveNeuralRendering(preferences.neuralRendering),0);
    if(!app::neuralRenderingAvailable)EnableWindow(neuralBox,FALSE);
    theme::name(neuralBox,L"AMD neural rendering");
    make(L"BUTTON",L"NR settings / Model",BS_PUSHBUTTON|WS_TABSTOP,47);
    if(!app::neuralRenderingAvailable)EnableWindow(designControl(47),FALSE);
    helpIcon(hwnd,215,0,0);
    helpIcon(hwnd,216,0,0);
    for (auto *label :
         {L"Off · Recommended first", L"FSR 3.1.6 · 2x frames", L"FSR 4.0.2 · 2x frames (ML)",
          L"FSR 4 · Synchronized compatibility test",L"FSR 3 · Synchronized compatibility test"})
        SendMessageW(fgBox, CB_ADDSTRING, 0, LPARAM(label));
    SendMessageW(fgBox, CB_SETCURSEL, app::effectiveFrameGeneration(preferences.frameGeneration),
                 0);
    if (!app::frameGenerationAvailable) {
        SendMessageW(fgBox, CB_RESETCONTENT, 0, 0);
        SendMessageW(fgBox, CB_ADDSTRING, 0, LPARAM(L"Off · Temporarily suspended"));
        SendMessageW(fgBox, CB_SETCURSEL, 0, 0);
        EnableWindow(fgBox, FALSE);
    }
#ifdef XPFSR_NATIVE_MOTION
    motionBox = combo(45);
    for (auto *label : {L"Camera-derived · Default", L"Native renderer · EXPERIMENTAL"})
        SendMessageW(motionBox, CB_ADDSTRING, 0, LPARAM(label));
    SendMessageW(motionBox, CB_SETCURSEL, preferences.motionSource, 0);
#endif
    auto check = [&](const wchar_t *label, int id, bool selected) {
        auto h = make(L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, id);
        SendMessageW(h, BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
        return h;
    };
    toggle = check(L"Enable FSR processing", 22, preferences.enabled);
    ghostBox = check(L"Reduce FSR 3 trails", 42, preferences.ghostReduction);
    cockpitBox = check(L"Cockpit-screen restore", 43, preferences.cockpitRestore);
    tintBox = check(L"Screen alignment tint", 44, preferences.alignmentTint);
    make(L"BUTTON", L"Launch X-Plane with FSR", BS_DEFPUSHBUTTON | WS_TABSTOP, 20);
    make(L"BUTTON", L"Native comparison", BS_PUSHBUTTON | WS_TABSTOP, 21);
    make(L"BUTTON", L"Restore graphics", BS_PUSHBUTTON | WS_TABSTOP, 107);
    injectionBanner = make(L"STATIC", L"", SS_LEFT, 30 + 500);
    theme::face(injectionBanner, 3);
    statusBox =
        control(L"EDIT",
                L"Launch X-Plane from this app, then load a flight. Live telemetry will confirm "
                L"when FSR is active.\r\n\r\nGPU checks do not prove live-flight visual quality.",
                ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, 0, 300, 120, 30);
    theme::face(statusBox, 2);
    createGraphicsControls(hwnd);
    theme::face(graphicsStatus, 2);
    theme::tone(graphicsStatus, theme::muted);
    motionSetupNote = make(L"STATIC", designSetupHint(), 0, 539);
    small(motionSetupNote);
    theme::surface(motionSetupNote, 1);
    for (auto item : {std::pair<int, const wchar_t *>{34, L"AA check"},
                      {36, L"FG check"},
                      {37, L"Screen check"},
                      {38, L"Comparison"},
                      {31, L"Renderer log"},
                      {32, L"Remove camera plugin"}})
        make(L"BUTTON", item.second, BS_PUSHBUTTON | WS_TABSTOP, item.first);
    if (!app::frameGenerationAvailable)
        EnableWindow(designControl(36), FALSE);
    auto disclaimer = make(L"STATIC", L"Test the same scene before judging quality.", 0, 540);
    small(disclaimer);
    footerStatus = make(L"STATIC", L"NOT RUNNING\r\nLaunch from this app", 0, 541);
    theme::surface(footerStatus, 1);
    savedLabel = make(L"STATIC", L"Your saved choices are restored", 0, 542);
    small(savedLabel, theme::green);
    theme::surface(savedLabel, 1);
    for (int id = 200; id <= 213; ++id)
        helpIcon(hwnd, id, 0, 0);
    for (auto item : {std::pair<HWND, const wchar_t *>{pathBox, L"X-Plane folder"},
                      {argsBox, L"Optional launch arguments"},
                      {presetBox, L"Resolution preset"},
                      {modeBox, L"FSR mode"},
                      {fgBox, L"Frame generation"},
                      {msaaBox, L"MSAA"},
                      {scaleBox, L"Input scale"},
                      {outputBox, L"Output resolution"},
                      {statusBox, L"Live renderer details"}})
        theme::name(item.first, item.second);
#ifdef XPFSR_NATIVE_MOTION
    helpIcon(hwnd, 214, 0, 0);
    theme::name(motionBox, L"Motion source");
#endif
    auto canvasClass = launcherWindowClass + L"_Canvas";
    WNDCLASSW canvasDefinition{};
    canvasDefinition.lpfnWndProc = designCanvasProc;
    canvasDefinition.hInstance = GetModuleHandleW(nullptr);
    canvasDefinition.lpszClassName = canvasClass.c_str();
    canvasDefinition.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&canvasDefinition);
    designCanvas = CreateWindowExW(WS_EX_CONTROLPARENT, canvasClass.c_str(), L"Rendering controls",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                   100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    ensure(designCanvas != nullptr, "Cannot create the launcher content viewport");
    std::vector<HWND> bodyControls;
    for (auto h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (h != designCanvas && h != footerStatus && h != savedLabel && GetDlgCtrlID(h) != 20 &&
            GetDlgCtrlID(h) != 21)
            bodyControls.push_back(h);
    for (auto h : bodyControls)
        SetParent(h, designCanvas);
    controlsReady = true;
    designLayout(hwnd);
    NOTIFYICONDATAW tray{sizeof(tray)};
    tray.hWnd = hwnd;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    tray.uCallbackMessage = WM_APP + 1;
    tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wcscpy_s(tray.szTip, L"FSR 3/4 + Frame Gen · Live simulator status");
    Shell_NotifyIconW(NIM_ADD, &tray);
    runtimeBanner(launcher::RuntimeState::stopped, {});
    SetTimer(hwnd, 1, 1000, nullptr);
    autoCheckUpdates = launcher::readSetting(appDirectory / L"settings.ini", L"Updates",
                                             L"AutomaticChecks", L"1") != L"0";
    releaseChannel = updates::loadChannel(appDirectory / L"settings.ini");
    SetTimer(hwnd, 3, 1500, nullptr);
    SetTimer(hwnd, 4, 500, nullptr);
}
