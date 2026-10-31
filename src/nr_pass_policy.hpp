// SPDX-License-Identifier: MIT
#pragma once
#include "nr_controls.hpp"
#include <algorithm>
namespace xpfsr::nr {
inline int effectivePasses(const Controls &controls,int capacity){
    return std::clamp(std::min(controls[Passes],capacity),1,3);
}
// Detail is the total composition budget, rather than a budget to repeat with
// every model evaluation. The upstream cascade still executes N times, then
// composes once against its untouched full-resolution input. Normalize that
// final edit to prevent repeated model smoothing from overwhelming the input.
// One pass retains the previous expression and output exactly.
inline float multipassDetailStrength(const Controls &controls,int capacity){
    return controls[Detail]/(100.f*float(effectivePasses(controls,capacity)));
}
} // namespace xpfsr::nr
