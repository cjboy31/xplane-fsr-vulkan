// SPDX-License-Identifier: MIT
#include "dispatch_cache.hpp"
#include "pacing.hpp"
#include <cassert>
#include <iostream>
static int one() { return 1; }
static int two() { return 2; }
int main() {
    using Fn = int (*)();
    unsigned queries = 0;
    auto a = xpfsr::nextDispatchIdentity(), b = xpfsr::nextDispatchIdentity();
    for (unsigned n = 0; n < 100000; ++n) {
        auto f = xpfsr::cachedDeviceFunction<Fn>(a, "same", [&] {
            ++queries;
            return one;
        });
        assert(f() == 1);
    }
    assert(queries == 1);
    assert(xpfsr::cachedDeviceFunction<Fn>(b, "same", [&] {
               ++queries;
               return two;
           })() == 2);
    assert(xpfsr::cachedDeviceFunction<Fn>(b, "alias", [&] {
               ++queries;
               return one;
           })() == 1);
    assert(queries == 3);
    xpfsr::Pacing p;
    for (unsigned n = 0; n < 600; ++n)
        p.add(100 + n * 16.0, 102 + n * 16.0, true);
    auto s = p.summary();
    assert(s.samples == 512 && s.p50 == 16 && s.p95 == 16 && s.presentP95 == 2 &&
           s.missingScene == 0);
    p.add(100 + 599 * 16.0 + 120, 100 + 599 * 16.0 + 125, false);
    s = p.summary();
    assert(s.maximum == 120 && s.over50ms == 1 && s.missingScene == 1);
    p.add(0, -1, false);
    assert(p.summary().missingScene == 1);
    std::cout << "PASS: 100000 steady-device calls use one lookup; device/alias changes refresh; "
                 "pacing window, percentiles and missing-scene counts\n";
}
