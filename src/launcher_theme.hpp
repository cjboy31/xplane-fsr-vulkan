// SPDX-License-Identifier: MIT
// Native Windows presentation only. No renderer, process or preference policy.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <oleacc.h>
#include <algorithm>
#include <string>
namespace theme {
inline constexpr COLORREF background = RGB(12, 16, 23), card = RGB(21, 27, 37),
                          field = RGB(28, 35, 47), border = RGB(49, 59, 75),
                          ink = RGB(241, 238, 229), muted = RGB(161, 173, 191),
                          gold = RGB(222, 194, 145), green = RGB(130, 221, 175),
                          red = RGB(245, 150, 145);
inline UINT dpi = 96;
inline ULONG_PTR gdip{};
inline HBRUSH backgroundBrush{}, cardBrush{}, fieldBrush{};
inline HFONT fonts[6]{};
inline IAccPropServices *accessibility{};
inline void name(HWND h, const wchar_t *s) {
    if (accessibility)
        accessibility->SetHwndPropStr(h, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME, s);
}
inline void enableDpi() {
    using Fn = BOOL(WINAPI *)(HANDLE);
    auto fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn)
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
inline UINT systemDpi() {
    using Fn = UINT(WINAPI *)();
    auto fn =
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (fn)
        return fn();
    auto dc = GetDC(nullptr);
    auto value = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return value > 0 ? UINT(value) : 96;
}
inline UINT windowDpi(HWND h) {
    using Fn = UINT(WINAPI *)(HWND);
    auto fn =
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return fn ? fn(h) : systemDpi();
}
inline int scrollbarWidth() {
    using Fn = int(WINAPI *)(int, UINT);
    auto fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    return fn ? fn(SM_CXVSCROLL, dpi) : MulDiv(GetSystemMetrics(SM_CXVSCROLL), dpi, systemDpi());
}
inline int px(int value) { return MulDiv(value, int(dpi), 96); }
inline Gdiplus::Color color(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}
inline void rounded(HDC dc, const RECT &r, COLORREF fill, COLORREF stroke, int radius = 10) {
    float x = float(r.left), y = float(r.top), w = float(r.right - r.left - 1),
          h = float(r.bottom - r.top - 1);
    if (w < 1 || h < 1)
        return;
    float d = std::min({float(px(radius * 2)), w, h});
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    RECT clip{};
    if (GetClipBox(dc, &clip) == NULLREGION)
        return;
    graphics.SetClip(
        Gdiplus::Rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top),
        Gdiplus::CombineModeIntersect);
    Gdiplus::GraphicsPath p;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    Gdiplus::SolidBrush brush(color(fill));
    graphics.FillPath(&brush, &p);
    if (stroke != fill) {
        Gdiplus::Pen pen(color(stroke), 1.f);
        graphics.DrawPath(&pen, &p);
    }
}
inline void makeFonts() {
    for (auto &f : fonts) {
        if (f)
            DeleteObject(f);
        f = nullptr;
    }
    const int sizes[] = {14, 32, 12, 17, 13, 26};
    for (int i = 0; i < 6; ++i)
        fonts[i] = CreateFontW(-px(sizes[i]), 0, 0, 0,
                               i == 1 || i == 3 || i == 5 ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                               i == 4 ? L"Consolas" : L"Segoe UI");
}
inline void caption(HWND h) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
    DWORD corner = 2;
    DwmSetWindowAttribute(h, 33, &corner, sizeof(corner));
}
inline void initialize(HWND h) {
    dpi = windowDpi(h);
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdip, &input, nullptr);
    backgroundBrush = CreateSolidBrush(background);
    cardBrush = CreateSolidBrush(card);
    fieldBrush = CreateSolidBrush(field);
    makeFonts();
    caption(h);
    CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices,
                     reinterpret_cast<void **>(&accessibility));
}
inline void shutdown() {
    if (accessibility) {
        accessibility->Release();
        accessibility = nullptr;
    }
    for (auto f : fonts)
        if (f)
            DeleteObject(f);
    DeleteObject(backgroundBrush);
    DeleteObject(cardBrush);
    DeleteObject(fieldBrush);
    if (gdip)
        Gdiplus::GdiplusShutdown(gdip);
}
inline void face(HWND h, int n) {
    SetPropW(h, L"XPFSRFont", HANDLE(INT_PTR(n)));
    SendMessageW(h, WM_SETFONT, WPARAM(fonts[n]), TRUE);
}
inline void surface(HWND h, int n) { SetPropW(h, L"XPFSRSurface", HANDLE(INT_PTR(n))); }
inline std::wstring label(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
struct ControlState {
    bool hover{};
    int kind{};
};
inline void text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c,
                 UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    auto old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
inline LRESULT CALLBACK controlProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *s = reinterpret_cast<ControlState *>(data);
    if (m == WM_KEYDOWN && w == VK_ESCAPE && GetPropW(GetAncestor(h, GA_ROOT), L"XPFSRHelp")) {
        SendMessageW(GetAncestor(h, GA_ROOT), WM_CLOSE, 0, 0);
        return 0;
    }
    if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, controlProc, 1);
        delete s;
        return DefSubclassProc(h, m, w, l);
    }
    if (m == WM_SIZE && s->kind == 3 && (GetWindowLongPtrW(h, GWL_STYLE) & ES_MULTILINE)) {
        auto result = DefSubclassProc(h, m, w, l);
        RECT rect;
        GetClientRect(h, &rect);
        InflateRect(&rect, -px(10), -px(6));
        SendMessageW(h, EM_SETRECT, 0, LPARAM(&rect));
        return result;
    }
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS || m == BM_SETCHECK || m == WM_ENABLE) {
        auto result = DefSubclassProc(h, m, w, l);
        InvalidateRect(h, nullptr, FALSE);
        if (m == WM_SETFOCUS)
            PostMessageW(GetAncestor(h, GA_ROOT), WM_APP + 6, 0, LPARAM(h));
        return result;
    }
    if (m == WM_MOUSEMOVE && !s->hover) {
        s->hover = true;
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_MOUSELEAVE) {
        s->hover = false;
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_MOUSEWHEEL && s->kind == 2 && !SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0))
        return SendMessageW(GetAncestor(h, GA_ROOT), m, w, l);
    if (m == WM_ERASEBKGND && s->kind != 3)
        return 1;
    if (m == WM_PAINT && s->kind != 3) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(h, &paint);
        RECT r;
        GetClientRect(h, &r);
        int id = GetDlgCtrlID(h);
        bool primary = id == 20, enabled = IsWindowEnabled(h), focus = GetFocus() == h;
        auto style = GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK;
        bool check = s->kind == 1 && (style == BS_AUTOCHECKBOX || style == BS_CHECKBOX);
        bool pressed = (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        auto base = GetPropW(h, L"XPFSRSurface") == HANDLE(INT_PTR(1)) ? background : card;
        auto brush = CreateSolidBrush(base);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        auto captionText = label(h);
        auto fg = enabled ? ink : muted;
        if (check) {
            RECT sw{px(2), std::max(0, (int(r.bottom) - px(18)) / 2), px(32),
                    std::max(0, (int(r.bottom) - px(18)) / 2) + px(18)};
            bool checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            rounded(dc, sw, checked ? gold : field, focus ? gold : border, 9);
            int x = checked ? sw.right - px(15) : sw.left + px(3);
            RECT dot{x, sw.top + px(3), x + px(12), sw.bottom - px(3)};
            rounded(dc, dot, checked ? background : muted, checked ? background : muted, 6);
            RECT tr{px(44), 0, r.right - px(2), r.bottom};
            text(dc, captionText.c_str(), tr, fonts[0], fg);
            if (focus) {
                RECT ring{px(39), px(1), r.right - px(1), r.bottom - px(1)};
                DrawFocusRect(dc, &ring);
            }
        } else {
            COLORREF fill = primary ? (pressed ? RGB(198, 167, 118) : gold)
                                    : (s->hover ? RGB(40, 49, 64) : field);
            rounded(dc, r, fill, focus ? gold : (primary ? fill : border),
                    id >= 200 && id <= 216 ? 12 : 9);
            if (s->kind == 2) {
                RECT tr{px(13), 0, r.right - px(32), r.bottom};
                text(dc, captionText.c_str(), tr, fonts[0], fg);
                HPEN pen = CreatePen(PS_SOLID, px(1), muted);
                auto old = SelectObject(dc, pen);
                int x = r.right - px(18), y = r.bottom / 2;
                MoveToEx(dc, x - px(4), y - px(2), nullptr);
                LineTo(dc, x, y + px(2));
                LineTo(dc, x + px(4), y - px(2));
                SelectObject(dc, old);
                DeleteObject(pen);
            } else {
                text(dc, captionText.c_str(), r, fonts[0],
                     primary ? background : (id == 32 ? muted : fg),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        EndPaint(h, &paint);
        return 0;
    }
    auto result = DefSubclassProc(h, m, w, l);
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_KEYDOWN || m == WM_KEYUP ||
        m == CB_SETCURSEL || m == WM_SETTEXT)
        InvalidateRect(h, nullptr, FALSE);
    return result;
}
inline void apply(HWND h, const wchar_t *type) {
    SendMessageW(h, WM_SETFONT, WPARAM(fonts[0]), TRUE);
    int kind = !wcscmp(type, L"BUTTON")     ? 1
               : !wcscmp(type, L"COMBOBOX") ? 2
               : !wcscmp(type, L"EDIT")     ? 3
                                            : 0;
    if (kind) {
        auto *s = new ControlState;
        s->kind = kind;
        SetWindowSubclass(h, controlProc, 1, DWORD_PTR(s));
    }
    if (kind == 2) {
        SendMessageW(h, CB_SETITEMHEIGHT, WPARAM(-1), px(28));
        SendMessageW(h, CB_SETITEMHEIGHT, 0, px(30));
    }
    if (kind == 3) {
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(px(10), px(10)));
        if (GetWindowLongPtrW(h, GWL_STYLE) & ES_MULTILINE) {
            RECT rect;
            GetClientRect(h, &rect);
            InflateRect(&rect, -px(10), -px(6));
            SendMessageW(h, EM_SETRECT, 0, LPARAM(&rect));
        }
    }
}
inline LRESULT colors(UINT m, WPARAM w, LPARAM l) {
    HWND h = reinterpret_cast<HWND>(l);
    HDC dc = reinterpret_cast<HDC>(w);
    int surfaceCode = int(INT_PTR(GetPropW(h, L"XPFSRSurface")));
    auto bg = surfaceCode == 1 ? background : card;
    auto brush = surfaceCode == 1 ? backgroundBrush : cardBrush;
    if (m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || GetDlgCtrlID(h) == 30) {
        bg = field;
        brush = fieldBrush;
    }
    COLORREF fg = ink;
    if (auto tone = GetPropW(h, L"XPFSRTone"))
        fg = COLORREF(UINT_PTR(tone) - 1);
    SetTextColor(dc, fg);
    SetBkColor(dc, bg);
    SetBkMode(dc, OPAQUE);
    return LRESULT(brush);
}
inline void tone(HWND h, COLORREF color) {
    SetPropW(h, L"XPFSRTone", HANDLE(UINT_PTR(color) + 1));
    InvalidateRect(h, nullptr, FALSE);
}
inline bool drawItem(DRAWITEMSTRUCT *d) {
    if (!d || d->CtlType != ODT_COMBOBOX)
        return false;
    auto selected = (d->itemState & ODS_SELECTED) != 0;
    auto brush = CreateSolidBrush(selected ? RGB(48, 57, 72) : field);
    FillRect(d->hDC, &d->rcItem, brush);
    DeleteObject(brush);
    std::wstring value;
    if (d->itemID != UINT(-1)) {
        auto n = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
        if (n >= 0) {
            value.resize(size_t(n) + 1);
            SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, LPARAM(value.data()));
            value.resize(size_t(n));
        }
    }
    RECT r = d->rcItem;
    r.left += px(12);
    r.right -= px(8);
    text(d->hDC, value.c_str(), r, fonts[0], selected ? gold : ink);
    return true;
}
inline void dpiChanged(HWND h, UINT value) {
    dpi = value;
    makeFonts();
    EnumChildWindows(
        h,
        [](HWND c, LPARAM) -> BOOL {
            int n = int(INT_PTR(GetPropW(c, L"XPFSRFont")));
            SendMessageW(c, WM_SETFONT, WPARAM(fonts[std::clamp(n, 0, 5)]), TRUE);
            wchar_t type[32]{};
            GetClassNameW(c, type, 32);
            if (!wcscmp(type, L"COMBOBOX")) {
                SendMessageW(c, CB_SETITEMHEIGHT, WPARAM(-1), px(28));
                SendMessageW(c, CB_SETITEMHEIGHT, 0, px(30));
            }
            return TRUE;
        },
        0);
}
} // namespace theme
