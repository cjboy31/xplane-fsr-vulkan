// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace xpfsr {
struct PacingSummary {
    double p50{}, p95{}, p99{}, maximum{}, presentP95{};
    unsigned samples{}, over50ms{}, missingScene{};
};
class Pacing {
    std::array<double, 512> intervals_{}, present_{};
    unsigned count_{}, cursor_{}, missing_{};
    double lastStart_{};

  public:
    void add(double startMs, double endMs, bool submittedScene) {
        if (!std::isfinite(startMs) || !std::isfinite(endMs) || endMs < startMs)
            return;
        if (lastStart_ > 0 && startMs > lastStart_) {
            intervals_[cursor_] = startMs - lastStart_;
            present_[cursor_] = endMs - startMs;
            cursor_ = (cursor_ + 1) % intervals_.size();
            count_ = std::min(count_ + 1, unsigned(intervals_.size()));
        }
        if (!submittedScene)
            ++missing_;
        lastStart_ = startMs;
    }
    PacingSummary summary() const {
        PacingSummary s{};
        s.samples = count_;
        s.missingScene = missing_;
        if (!count_)
            return s;
        auto a = intervals_, b = present_;
        std::sort(a.begin(), a.begin() + count_);
        std::sort(b.begin(), b.begin() + count_);
        auto percentile = [&](double p) { return unsigned(std::ceil(p * count_)) - 1; };
        s.p50 = a[percentile(.50)];
        s.p95 = a[percentile(.95)];
        s.p99 = a[percentile(.99)];
        s.maximum = a[count_ - 1];
        s.presentP95 = b[percentile(.95)];
        s.over50ms =
            unsigned(std::count_if(a.begin(), a.begin() + count_, [](double v) { return v > 50; }));
        return s;
    }
};
} // namespace xpfsr
