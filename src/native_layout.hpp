// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <algorithm>
#include <map>
#include <vector>

namespace theme {
// Moving partially clipped child windows must not copy their old visible pixels.
// Commit each sibling group together, then invalidate the complete control tree.
// The composited host presents that tree only after all descendants have painted.
class Layout {
    struct Move { HWND window; int x, y, width, height; };
    std::map<HWND, std::vector<Move>> groups;
    static constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS |
                                  SWP_NOREDRAW | SWP_SHOWWINDOW;
public:
    void place(HWND h, int x, int y, int width, int height) {
        if (h)
            groups[GetParent(h)].push_back({h, x, y, std::max(1, width), std::max(1, height)});
    }
    void commit(HWND host) {
        for (auto &group : groups) {
            auto batch = BeginDeferWindowPos(int(group.second.size()));
            for (const auto &m : group.second) {
                if (!batch)
                    break;
                batch = DeferWindowPos(batch, m.window, nullptr, m.x, m.y, m.width, m.height, flags);
            }
            const bool moved = batch && EndDeferWindowPos(batch);
            if (!moved)
                for (const auto &m : group.second)
                    SetWindowPos(m.window, nullptr, m.x, m.y, m.width, m.height, flags);
        }
        groups.clear();
        RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
};
} // namespace theme
