// SPDX-License-Identifier: MIT
#pragma once
#include "background_files.hpp"
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace xpfsr {
class RendererFiles {
    BackgroundFiles bridge;
    std::array<bool, 2> outputs{};
    static std::filesystem::path environmentPath(const wchar_t *name) {
        wchar_t value[32768]{};
        auto count = GetEnvironmentVariableW(name, value, 32768);
        return count && count < 32768 ? std::filesystem::path(std::wstring(value, count))
                                     : std::filesystem::path{};
    }
public:
    void start() {
        const std::array<std::filesystem::path, 2> controls{
            environmentPath(L"XPFSR24_CONTROL"), environmentPath(L"XPFSR24_FG_CONTROL")};
        const std::array<std::filesystem::path, 2> statuses{
            environmentPath(L"XPFSR24_STATUS"), environmentPath(L"XPFSR24_NR_STATUS")};
        for (size_t i = 0; i < outputs.size(); ++i) outputs[i] = !statuses[i].empty();
        if (controls[0].empty() && controls[1].empty() && !outputs[0] && !outputs[1]) return;
        bridge.start([controls] {
            BackgroundFiles::Controls values{-1, -1};
            for (size_t i = 0; i < controls.size(); ++i) {
                if (controls[i].empty()) continue;
                std::ifstream input(controls[i]);
                char value{};
                if (input.get(value) && (value == '0' || value == '1')) values[i] = value - '0';
            }
            return values;
        }, [statuses](size_t output, const std::string &snapshot) {
            if (statuses[output].empty()) return;
            auto temporary = statuses[output].wstring() + L".tmp";
            {
                std::ofstream stream(std::filesystem::path(temporary), std::ios::trunc);
                stream.write(snapshot.data(), std::streamsize(snapshot.size()));
                stream.close();
                if (!stream) return;
            }
            // Preserve atomic replacement and the old NR durability request,
            // but perform them solely on the worker, never the render thread.
            MoveFileExW(temporary.c_str(), statuses[output].c_str(),
                        MOVEFILE_REPLACE_EXISTING | (output ? MOVEFILE_WRITE_THROUGH : 0));
        });
    }
    bool hasOutput(size_t output) const { return output < outputs.size() && outputs[output]; }
    bool offer(size_t output, std::string value) { return hasOutput(output) && bridge.offer(output, std::move(value)); }
    int control(size_t index) const noexcept { return bridge.control(index); }
    uint64_t controlSnapshot(size_t index) const noexcept { return bridge.controlSnapshot(index); }
    void stop() noexcept { bridge.stop(); }
};
}
