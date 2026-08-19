#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

struct Percentiles {
    double p50 = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0;
};

// Nearest-rank percentiles. Takes the vector by value: sorting a copy keeps
// the caller's sample order intact.
inline Percentiles computePercentiles(std::vector<double> samples) {
    Percentiles p;
    if (samples.empty()) return p;
    std::sort(samples.begin(), samples.end());

    const auto pick = [&samples](double fraction) {
        // Standard nearest-rank: ceil(fraction * N) - 1, floored at 0 so a
        // degenerate case can't underflow. (A plain floor(fraction * N), as
        // this used to read, collapses p99 into max whenever N is a
        // multiple of 100 -- e.g. N=100 gives idx=99 for both.)
        long idx = static_cast<long>(std::ceil(fraction * static_cast<double>(samples.size()))) - 1;
        if (idx < 0) idx = 0;
        size_t uidx = static_cast<size_t>(idx);
        if (uidx >= samples.size()) uidx = samples.size() - 1;
        return samples[uidx];
    };

    p.p50 = pick(0.50);
    p.p95 = pick(0.95);
    p.p99 = pick(0.99);
    p.max = samples.back();
    return p;
}
