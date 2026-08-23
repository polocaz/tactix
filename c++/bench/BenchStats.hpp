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

// Arithmetic mean, which is the ONLY statistic here that a per-phase breakdown
// can legitimately be built out of.
//
// Percentiles do not add: the p50 of the tick is not the sum of the phases'
// p50s, because the slowest phase on the median tick is not the same phase on
// every tick, and a phase's own median tick is generally not the tick whose
// total lands on the median. Summing them produces a number that looks like a
// tick cost, is not one, and is wrong by however much the phases' spikes fail
// to line up. Means do add, exactly: the mean of the sums is the sum of the
// means, for any samples at all. So the share-of-tick column is computed from
// means, and the percentiles are reported alongside for tail shape only.
inline double computeMean(const std::vector<double>& samples) {
    if (samples.empty()) return 0.0;
    double total = 0.0;
    for (double s : samples) total += s;
    return total / static_cast<double>(samples.size());
}
