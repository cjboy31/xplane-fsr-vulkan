// SPDX-License-Identifier: MIT
// Portable CPU scheduling plan checks. Build: c++ -std=c++17 src/cpu_scheduling_tests.cpp
#include "cpu_scheduling.hpp"
#include <cstdio>
#include <cstdlib>
using namespace xpfsr::cpu;
static void check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static std::vector<LogicalCpu> make(unsigned count, uint8_t efficiency, uint8_t group, uint32_t kb, uint32_t first = 0) {
    std::vector<LogicalCpu> out;
    for (unsigned i = 0; i < count; ++i) out.push_back({first + i, efficiency, group, kb});
    return out;
}
static std::vector<LogicalCpu> join(std::vector<LogicalCpu> a, const std::vector<LogicalCpu> &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
int main() {
    // Intel hybrid (e.g. 8 P-cores with HT + 16 E-cores): only P-core threads.
    auto hybrid = join(make(16, 1, 0, 36864), make(16, 0, 0, 36864, 16));
    auto p = plan(hybrid);
    check(p.ids.size() == 16 && p.ids.front() == 0 && p.ids.back() == 15, "Hybrid keeps exactly the P-core threads");
    check(p.description.find("performance cores") != std::string::npos, "Hybrid plan explains itself");

    // Dual-CCD X3D (e.g. 9950X3D): 96 MB on CCD0, 32 MB on CCD1.
    auto x3d = join(make(16, 0, 0, 98304), make(16, 0, 1, 32768, 16));
    p = plan(x3d);
    check(p.ids.size() == 16 && p.ids.front() == 0 && p.ids.back() == 15, "X3D keeps the larger-cache CCD");
    check(p.description.find("larger-cache") != std::string::npos, "X3D plan explains itself");

    // Ordinary dual-CCD (9950X): equal caches, no change.
    p = plan(join(make(16, 0, 0, 32768), make(16, 0, 1, 32768, 16)));
    check(p.ids.empty() && p.description.find("same kind") != std::string::npos, "Equal CCDs left to Windows");

    // Single-CCD X3D (9850X3D): one cache group, no change.
    p = plan(make(16, 0, 0, 98304));
    check(p.ids.empty(), "Single-CCD CPU left to Windows");

    // Tiny systems and processors reserved by others are never squeezed.
    check(plan(make(2, 0, 0, 8192)).ids.empty(), "Fewer than 4 processors: no change");
    auto small = join(make(2, 1, 0, 12288), make(8, 0, 0, 12288, 2));
    check(plan(small).ids.empty(), "Hybrid with only 2 fast threads: no change");
    auto reserved = hybrid;
    for (auto &c : reserved) if (c.efficiencyClass == 1 && c.id >= 4) c.allocated = true;
    p = plan(reserved);
    check(p.ids.size() == 4, "Processors reserved by another process are skipped");
    check(plan({}).ids.empty(), "Empty report: no change");
    std::puts("PASS: CPU scheduling plans for hybrid, dual-CCD X3D, equal CCDs, single CCD and small systems");
}
