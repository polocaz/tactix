#include "Simulation.hpp"
#include "WorkCounters.hpp"
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

    std::vector<double> tickMs;
    tickMs.reserve(static_cast<size_t>(ticks));

    // NOTE: the four WorkCounters atomics share a cache line and are
    // incremented ~216,000 times/tick from worker threads (a known,
    // deliberately parked contention point — see WorkCounters.hpp / task 8
    // review). The timings collected below therefore include that counter
    // instrumentation overhead, not just the simulation's own cost. Fixing
    // it requires reshaping a signature a later task rewrites wholesale, so
    // it is left as-is here; any README publishing these numbers must carry
    // this caveat forward.
    for (int i = 0; i < ticks; ++i) {
        const auto start = std::chrono::steady_clock::now();
        sim.tick(1.0f / 60.0f);
        const auto end = std::chrono::steady_clock::now();
        tickMs.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    const Percentiles p = computePercentiles(tickMs);
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
            "  \"projectileHitTests\": %llu,\n"
            "  \"stateDigest\": \"%016llx\"\n"
            "}\n",
            agents, ticks, seed, threads, width, height,
            p.p50, p.p95, p.p99, p.max,
            (unsigned long long)c.candidatesExamined.load(),
            (unsigned long long)c.cellsVisited.load(),
            (unsigned long long)c.gridInsertions.load(),
            (unsigned long long)c.jobsDispatched.load(),
            (unsigned long long)c.squadDecisions.load(),
            (unsigned long long)c.projectileHitTests.load(),
            (unsigned long long)digest);
    } else {
        std::printf("agents=%d ticks=%d seed=%d threads=%d width=%d height=%d\n",
                    agents, ticks, seed, threads, width, height);
        std::printf("tick ms   p50=%.4f  p95=%.4f  p99=%.4f  max=%.4f\n",
                    p.p50, p.p95, p.p99, p.max);
        std::printf("digest    %016llx\n", (unsigned long long)digest);
    }
    return 0;
}
