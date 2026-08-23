#include "Simulation.hpp"
#include "WorkCounters.hpp"
#include "TickProfile.hpp"
#include "BenchStats.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Parses --name as a base-10 integer. Exits (code 2) with a message on
// stderr if the flag is present with no following value, or if the value
// doesn't parse cleanly as an integer (garbage, empty, or out of int
// range) -- a mistyped flag should fail loudly, not silently become 0 or
// the default. Returns `fallback` if the flag is absent entirely.
int intArg(int argc, char** argv, const char* name, int fallback) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) != 0) continue;
        if (i + 1 >= argc) {
            std::fprintf(stderr, "error: %s requires a value\n", name);
            std::exit(2);
        }
        const char* value = argv[i + 1];
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end != '\0' || errno == ERANGE ||
            parsed < INT_MIN || parsed > INT_MAX) {
            std::fprintf(stderr, "error: %s value '%s' is not a valid integer\n", name, value);
            std::exit(2);
        }
        return static_cast<int>(parsed);
    }
    return fallback;
}

bool hasFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    // Diagnostics to stderr so stdout carries only the JSON payload.
    // Simulation/JobSystem log during construction, so this must run first.
    spdlog::set_default_logger(spdlog::stderr_color_mt("tactix"));

    const int agents  = intArg(argc, argv, "--agents", 10000);
    const int ticks   = intArg(argc, argv, "--ticks", 2000);
    const int seed    = intArg(argc, argv, "--seed", 42);
    const int threads = intArg(argc, argv, "--threads", 0);
    const int width   = intArg(argc, argv, "--width", 1280);
    const int height  = intArg(argc, argv, "--height", 720);
    const bool json   = hasFlag(argc, argv, "--json");
    // Opt-in, so the headline tick cost is never quietly a profiled tick cost.
    // The two are separate commands and the profiled run reports its own
    // overhead rather than asking anyone to assume it is small.
    const bool profile = hasFlag(argc, argv, "--profile");

    if (agents <= 0 || ticks <= 0) {
        std::fprintf(stderr, "error: --agents and --ticks must be positive\n");
        return 2;
    }
    if (seed < 0) {
        std::fprintf(stderr, "error: --seed must be >= 0\n");
        return 2;
    }
    // Bounded, not just non-negative: JobSystem treats a huge --threads
    // as a literal worker count and tries to spawn that many OS threads.
    // Anything past 1024 is certainly a typo, not an intention.
    if (threads < 0 || threads > 1024) {
        std::fprintf(stderr, "error: --threads must be between 0 and 1024\n");
        return 2;
    }
    // 300 is not an arbitrary round number: generateObstacles() computes
    // rng.range(..., 100, width - 200) for buildings, which inverts below
    // ~300px wide and underflows to a roughly 4-billion-wide span. 300
    // keeps that range valid with margin.
    if (width < 300 || height < 300 || width > 100000 || height > 100000) {
        std::fprintf(stderr, "error: --width and --height must be between 300 and 100000\n");
        return 2;
    }

    Simulation sim(width, height,
                   static_cast<uint32_t>(seed),
                   static_cast<uint32_t>(threads));
    sim.init(static_cast<size_t>(agents));
    sim.setPaused(false);
    sim.resetCounters();

    sim.profileTicks = profile;

    std::vector<double> tickMs;
    tickMs.reserve(static_cast<size_t>(ticks));

    // One sample vector per phase, filled only when profiling. Collected into
    // plain vectors and reduced at the end rather than accumulated on the fly,
    // so the per-phase percentiles come from the same nearest-rank code path
    // the tick percentiles do.
    std::vector<std::vector<double>> phaseMs(TickProfile::kCount);
    if (profile) {
        for (auto& v : phaseMs) v.reserve(static_cast<size_t>(ticks));
    }

    // NOTE: the WorkCounters atomics still share a cache line, so the timings
    // below still include counter instrumentation overhead and not just the
    // simulation's own cost. The rate is far lower than it was: the two
    // counters that dominated the traffic are now accumulated in locals inside
    // SpatialHash::forEachCell and flushed once per query rather than once per
    // cell visited, about 43,000 contended increments a tick instead of about
    // 554,000. That change alone moved p50 from 7.69 ms to 3.71 ms at
    // --agents 10000, so the caveat is smaller but not gone, and any README
    // publishing these numbers must still carry it forward.
    for (int i = 0; i < ticks; ++i) {
        const auto start = std::chrono::steady_clock::now();
        sim.tick(1.0f / 60.0f);
        const auto end = std::chrono::steady_clock::now();
        tickMs.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());

        if (profile) {
            const TickProfile& tp = sim.lastTickProfile();
            for (int ph = 0; ph < TickProfile::kCount; ++ph) {
                phaseMs[static_cast<size_t>(ph)].push_back(tp.ms[ph]);
            }
        }
    }

    const Percentiles p = computePercentiles(tickMs);
    const double tickMean = computeMean(tickMs);

    // The phases sum to slightly less than the tick: the outermost clock reads
    // in main() bracket the whole call, while the innermost ones start just
    // inside it. Reported rather than hidden, because the gap is also where
    // any phase someone forgets to mark would show up.
    double phaseMeanTotal = 0.0;
    if (profile) {
        for (int ph = 0; ph < TickProfile::kCount; ++ph) {
            phaseMeanTotal += computeMean(phaseMs[static_cast<size_t>(ph)]);
        }
    }
    const double unattributedMs = profile ? (tickMean - phaseMeanTotal) : 0.0;
    const WorkCounters& c = sim.counters();
    const uint64_t digest = sim.stateDigest();

    if (json) {
        std::printf(
            "{\n"
            "  \"agents\": %d,\n"
            "  \"ticks\": %d,\n"
            "  \"seed\": %d,\n"
            "  \"threads\": %d,\n"
            "  \"width\": %d,\n"
            "  \"height\": %d,\n"
            "  \"p50Ms\": %.4f,\n"
            "  \"p95Ms\": %.4f,\n"
            "  \"p99Ms\": %.4f,\n"
            "  \"maxMs\": %.4f,\n"
            "  \"candidatesExamined\": %llu,\n"
            "  \"cellsVisited\": %llu,\n"
            "  \"gridInsertions\": %llu,\n"
            "  \"jobsDispatched\": %llu,\n"
            "  \"squadDecisions\": %llu,\n"
            "  \"armyDecisions\": %llu,\n"
            "  \"projectileHitTests\": %llu,\n"
            "  \"stateDigest\": \"%016llx\"%s\n",
            agents, ticks, seed, threads, width, height,
            p.p50, p.p95, p.p99, p.max,
            (unsigned long long)c.candidatesExamined.load(),
            (unsigned long long)c.cellsVisited.load(),
            (unsigned long long)c.gridInsertions.load(),
            (unsigned long long)c.jobsDispatched.load(),
            (unsigned long long)c.squadDecisions.load(),
            (unsigned long long)c.armyDecisions.load(),
            (unsigned long long)c.projectileHitTests.load(),
            (unsigned long long)digest,
            profile ? "," : "");

        if (profile) {
            std::printf("  \"tickMeanMs\": %.4f,\n"
                        "  \"unattributedMs\": %.4f,\n"
                        "  \"phases\": {\n",
                        tickMean, unattributedMs);
            for (int ph = 0; ph < TickProfile::kCount; ++ph) {
                const auto& samples = phaseMs[static_cast<size_t>(ph)];
                const Percentiles pp = computePercentiles(samples);
                const double mean = computeMean(samples);
                std::printf("    \"%s\": { \"meanMs\": %.4f, \"sharePct\": %.2f, "
                            "\"p50Ms\": %.4f, \"p95Ms\": %.4f, \"maxMs\": %.4f }%s\n",
                            TickProfile::name(static_cast<TickProfile::Phase>(ph)),
                            mean,
                            tickMean > 0.0 ? (mean / tickMean) * 100.0 : 0.0,
                            pp.p50, pp.p95, pp.max,
                            ph + 1 < TickProfile::kCount ? "," : "");
            }
            std::printf("  }\n");
        }
        std::printf("}\n");
    } else {
        std::printf("agents=%d ticks=%d seed=%d threads=%d width=%d height=%d\n",
                    agents, ticks, seed, threads, width, height);
        std::printf("tick ms   p50=%.4f  p95=%.4f  p99=%.4f  max=%.4f\n",
                    p.p50, p.p95, p.p99, p.max);
        std::printf("digest    %016llx\n", (unsigned long long)digest);

        if (profile) {
            // shareOfTick comes from the means, never the percentiles. See
            // computeMean in BenchStats.hpp for why that is not a style
            // preference: a column of percentile shares would not sum to 100
            // and would not mean anything if it did.
            std::printf("\ntick mean %.4f ms, phases in tick order:\n", tickMean);
            std::printf("  %-16s %9s %7s %9s %9s %9s\n",
                        "phase", "mean ms", "share", "p50 ms", "p95 ms", "max ms");
            for (int ph = 0; ph < TickProfile::kCount; ++ph) {
                const auto& samples = phaseMs[static_cast<size_t>(ph)];
                const Percentiles pp = computePercentiles(samples);
                const double mean = computeMean(samples);
                std::printf("  %-16s %9.4f %6.2f%% %9.4f %9.4f %9.4f\n",
                            TickProfile::name(static_cast<TickProfile::Phase>(ph)),
                            mean,
                            tickMean > 0.0 ? (mean / tickMean) * 100.0 : 0.0,
                            pp.p50, pp.p95, pp.max);
            }
            std::printf("  %-16s %9.4f %6.2f%%\n", "(unattributed)",
                        unattributedMs,
                        tickMean > 0.0 ? (unattributedMs / tickMean) * 100.0 : 0.0);
        }
    }
    return 0;
}
