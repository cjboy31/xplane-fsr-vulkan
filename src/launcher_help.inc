// SPDX-License-Identifier: MIT
struct HelpContext {
    HWND owner{}, previousFocus{};
    std::wstring title, body;
};
static LRESULT CALLBACK modernHelpProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto c = reinterpret_cast<HelpContext *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
    case WM_CREATE: {
        c = static_cast<HelpContext *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(c));
        SetPropW(h, L"XPFSRHelp", HANDLE(1));
        theme::caption(h);
        RECT r;
        GetClientRect(h, &r);
        int width = r.right, height = r.bottom;
        auto title = control(L"STATIC", c->title.c_str(), 0, theme::px(26), theme::px(24),
                             width - theme::px(52), theme::px(38), 1, h);
        theme::face(title, 3);
        auto body = control(L"EDIT", c->body.c_str(),
                            ES_READONLY | ES_MULTILINE | WS_VSCROLL | WS_TABSTOP, theme::px(26),
                            theme::px(82), width - theme::px(52), height - theme::px(166), 3, h);
        control(L"BUTTON", L"Done", BS_DEFPUSHBUTTON | WS_TABSTOP, width - theme::px(156),
                height - theme::px(60), theme::px(130), theme::px(36), 2, h);
        SetFocus(body);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 2) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return theme::colors(m, w, l);
    case WM_DRAWITEM:
        if (theme::drawItem(reinterpret_cast<DRAWITEMSTRUCT *>(l)))
            return TRUE;
        break;
    case WM_DESTROY:
        if (c) {
            EnableWindow(c->owner, TRUE);
            SetActiveWindow(c->owner);
            if (IsWindow(c->previousFocus))
                SetFocus(c->previousFocus);
            delete c;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void modernHelp(HWND owner, const wchar_t *title, const wchar_t *body) {
    auto name = launcherWindowClass + L"_ModernHelp";
    WNDCLASSW c{};
    c.lpfnWndProc = modernHelpProc;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = name.c_str();
    c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    c.hbrBackground = theme::cardBrush;
    RegisterClassW(&c);
    RECT parent;
    GetWindowRect(owner, &parent);
    int width = theme::px(680), height = theme::px(600);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    height = std::min(height, int(work.bottom - work.top - theme::px(32)));
    int x = std::clamp(int(parent.left + (parent.right - parent.left - width) / 2), int(work.left),
                       std::max(int(work.left), int(work.right - width)));
    int y = std::clamp(int(parent.top + (parent.bottom - parent.top - height) / 2), int(work.top),
                       std::max(int(work.top), int(work.bottom - height)));
    auto context = new HelpContext{owner, GetFocus(), title, body};
    auto h = CreateWindowExW(WS_EX_DLGMODALFRAME, name.c_str(), title,
                             WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, width, height, owner,
                             nullptr, GetModuleHandleW(nullptr), context);
    if (!h) {
        delete context;
        throw std::runtime_error("Could not open the setting help");
    }
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetActiveWindow(h);
    SetFocus(GetDlgItem(h, 3));
}
static void platformHelp(HWND owner) {
    modernHelp(owner, L"Platform support",
               L"WINDOWS 64-BIT / VULKAN\r\nThe current application, installer and renderer are "
               L"built for Windows. "
               L"FSR 4 and frame generation still require supported hardware, drivers and a "
               L"successful GPU check.\r\n\r\n"
               L"LINUX / PORT REQUIRED\r\nX-Plane uses Vulkan on Linux, so a native port is "
               L"feasible. This Windows package "
               L"does not work as a Linux plugin. It needs Linux libraries, shared memory, process "
               L"handling, an updater and "
               L"a supported FSR backend, followed by real Linux testing. FSR 4 / FG parity is not "
               L"confirmed.\r\n\r\n"
               L"macOS / SEPARATE METAL IMPLEMENTATION\r\nX-Plane uses Metal on Mac. This Vulkan "
               L"interception path cannot "
               L"attach to it; MoltenVK does not turn X-Plane's Metal renderer into Vulkan. A Mac "
               L"version needs a separate "
               L"renderer and Apple hardware testing. No Mac installer or compatibility claim is "
               L"provided.\r\n\r\n"
               L"The open-source project welcomes platform work. See docs/PLATFORMS.md for the "
               L"audit and port requirements.");
}
