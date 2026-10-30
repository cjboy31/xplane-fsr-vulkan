// SPDX-License-Identifier: MIT
// Exercise the real updater window procedure and native combo drawing into a
// memory bitmap. No desktop input, simulator process or network request.
#define wWinMain unusedLauncherMain
#include "launcher.cpp"
#undef wWinMain
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    try {
        ensure(argc == 2, "Fresh workspace fixture required");
        appDirectory = argv[1];
        ensure(!fs::exists(appDirectory), "Refusing to reuse a test fixture");
        fs::create_directories(appDirectory);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        window = CreateWindowW(L"STATIC", L"Hidden test owner", 0, 0, 0, 1, 1,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ensure(window != nullptr, "Cannot create hidden fixture owner");
        theme::initialize(window);
        WNDCLASSW c{};
        c.lpfnWndProc = updateProcedure;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"XPFSRUpdateDrawingRegression";
        RegisterClassW(&c);
        updateWindow = CreateWindowW(c.lpszClassName, L"Hidden updater fixture", 0,
                                     0, 0, 900, 550, window, nullptr, c.hInstance, nullptr);
        ensure(updateWindow != nullptr, "Cannot create hidden updater fixture");
        updateJob->phase = UpdateJob::checking; // Selection persistence without network.
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            theme::dpi = dpi;
            theme::makeFonts();
            channelBox = scaledControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                        20, 20, 650, 150, 151, updateWindow);
            const wchar_t *labels[] = {L"Normal - recommended / frame generation Off",
                                       L"Experimental - frame generation development"};
            for (auto label : labels)
                ensure(SendMessageW(channelBox, CB_ADDSTRING, 0, LPARAM(label)) >= 0,
                       "Cannot add channel option");
            ensure(SendMessageW(channelBox, CB_GETCOUNT, 0, 0) == 2,
                   "Both release-channel choices must exist");
            int width = theme::px(650), height = theme::px(30);
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
            void *pixels{};
            auto dc = CreateCompatibleDC(nullptr);
            auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            ensure(dc && bitmap && pixels, "Cannot allocate drawing fixture");
            auto old = SelectObject(dc, bitmap);
            for (UINT row = 0; row != 2; ++row) {
                wchar_t actual[128]{};
                SendMessageW(channelBox, CB_GETLBTEXT, row, LPARAM(actual));
                ensure(!wcscmp(actual, labels[row]), "A channel label is missing");
                for (UINT state : {0u, UINT(ODS_SELECTED | ODS_FOCUS)}) {
                    RECT rect{0, 0, width, height};
                    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                    DRAWITEMSTRUCT draw{};
                    draw.CtlType = ODT_COMBOBOX;
                    draw.CtlID = 151;
                    draw.itemID = row;
                    draw.itemAction = ODA_DRAWENTIRE;
                    draw.itemState = state;
                    draw.hwndItem = channelBox;
                    draw.hDC = dc;
                    draw.rcItem = rect;
                    ensure(SendMessageW(updateWindow, WM_DRAWITEM, 151, LPARAM(&draw)) == TRUE,
                           "Updater does not handle owner-drawn dropdown rows");
                    GdiFlush();
                    auto bg = state ? RGB(48, 57, 72) : theme::field;
                    auto packed = (GetRValue(bg) << 16) | (GetGValue(bg) << 8) | GetBValue(bg);
                    size_t textPixels{};
                    for (int y = 0; y != height; ++y)
                        for (int x = 0; x != width; ++x)
                            textPixels += (static_cast<DWORD *>(pixels)[y * width + x] & 0xffffff) != packed;
                    ensure(GetPixel(dc, 0, 0) == bg && textPixels > size_t(theme::px(30)),
                           "A dropdown row is blank or has an unpainted background");
                }
                SendMessageW(channelBox, CB_SETCURSEL, row, 0);
                SendMessageW(updateWindow, WM_COMMAND, MAKEWPARAM(151, CBN_SELCHANGE),
                             LPARAM(channelBox));
                ensure(updates::loadChannel(appDirectory / L"settings.ini") ==
                           (row ? updates::Channel::Experimental : updates::Channel::Normal),
                       "Selecting the visible channel did not persist");
            }
            MEASUREITEMSTRUCT measure{};
            measure.CtlType = ODT_COMBOBOX;
            measure.CtlID = 151;
            ensure(SendMessageW(updateWindow, WM_MEASUREITEM, 151, LPARAM(&measure)) == TRUE &&
                       measure.itemHeight == UINT(theme::px(30)),
                   "Updater does not measure dropdown rows for its DPI");
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
            DestroyWindow(channelBox);
            channelBox = nullptr;
            std::cout << "PASS: both channel rows painted, highlighted, measured and selected at "
                      << dpi << " DPI\n";
        }
        // Suppress normal application activation during isolated fixture teardown.
        SetWindowLongPtrW(updateWindow, GWLP_WNDPROC, LONG_PTR(DefWindowProcW));
        DestroyWindow(updateWindow);
        updateWindow = nullptr;
        DestroyWindow(window);
        theme::shutdown();
        CoUninitialize();
        std::cout << "PASS: updater dropdown regression; memory bitmaps and workspace INI only.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
