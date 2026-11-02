#pragma once
// v2 opt-in CPU scheduling helper (Windows). Pure planning logic lives here so
// it can be tested without the real processor; layer.cpp reads the processor
// with GetSystemCpuSetInformation / GetLogicalProcessorInformationEx and
// applies the plan with SetProcessDefaultCpuSets for this X-Plane process only.
// Off by default. Nothing is written to the system; the process default is
// cleared again when the renderer shuts down, and it ends with the process.
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xpfsr::cpu {

struct LogicalCpu {
    uint32_t id{};               // CPU set ID (what SetProcessDefaultCpuSets takes)
    uint8_t efficiencyClass{};   // higher = faster core (P-cores on Intel hybrid)
    uint8_t cacheGroup{};        // last-level cache index (one per CCD on Ryzen)
    uint32_t cacheKb{};          // size of that last-level cache
    bool parked{}, allocated{};  // parked by Windows / reserved for another process
};

struct Plan {
    std::vector<uint32_t> ids;   // empty = leave the process default unchanged
    std::string description;     // plain-language summary for the log and panel
};

inline Plan plan(const std::vector<LogicalCpu> &cpus) {
    Plan out;
    std::vector<LogicalCpu> usable;
    for (const auto &c : cpus)
        if (!c.allocated) usable.push_back(c);
    if (usable.size() < 4) {
        out.description = "too few processors were reported; no change";
        return out;
    }
    uint8_t lowest = 255, highest = 0;
    for (const auto &c : usable) {
        lowest = std::min(lowest, c.efficiencyClass);
        highest = std::max(highest, c.efficiencyClass);
    }
    auto finish = [&](std::vector<uint32_t> ids, const std::string &why) {
        // Never squeeze the sim onto a handful of threads.
        if (ids.size() < 4 || ids.size() == usable.size()) {
            out.description = why + "; fewer than 4 or all processors would remain, so no change";
            return out;
        }
        out.ids = std::move(ids);
        out.description = why + " (" + std::to_string(out.ids.size()) + " of " + std::to_string(usable.size()) +
                          " logical processors)";
        return out;
    };
    if (lowest != highest) {
        // Intel hybrid: keep X-Plane on the performance cores.
        std::vector<uint32_t> ids;
        for (const auto &c : usable)
            if (c.efficiencyClass == highest) ids.push_back(c.id);
        return finish(ids, "hybrid processor: X-Plane kept on the performance cores");
    }
    std::map<uint8_t, uint32_t> cache;  // group -> size
    for (const auto &c : usable) cache[c.cacheGroup] = std::max(cache[c.cacheGroup], c.cacheKb);
    if (cache.size() > 1) {
        uint32_t largest = 0, smallest = UINT32_MAX;
        for (const auto &[group, kb] : cache) {
            largest = std::max(largest, kb);
            smallest = std::min(smallest, kb);
        }
        // Dual-CCD X3D: one CCD has the stacked cache (at least 1.5x larger).
        if (smallest && largest >= smallest + smallest / 2) {
            std::vector<uint32_t> ids;
            for (const auto &c : usable)
                if (cache[c.cacheGroup] == largest) ids.push_back(c.id);
            return finish(ids, "processor with a larger-cache chiplet: X-Plane kept on that chiplet");
        }
    }
    out.description = "all cores are the same kind; processor choice left to Windows";
    return out;
}

}  // namespace xpfsr::cpu
