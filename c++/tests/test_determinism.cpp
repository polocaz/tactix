#include <doctest/doctest.h>
#include "Simulation.hpp"
#include <thread>

namespace {
// Runs a simulation to completion and returns the digest of its final state.
uint64_t runAndDigest(size_t agents, int ticks, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed);
    sim.init(agents);
    sim.setPaused(false);
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("same seed reproduces exactly") {
    CHECK(runAndDigest(2000, 200, 42u) == runAndDigest(2000, 200, 42u));
}

TEST_CASE("different seeds diverge") {
    CHECK(runAndDigest(2000, 200, 42u) != runAndDigest(2000, 200, 43u));
}

TEST_CASE("repeated runs stay stable across many trials") {
    const uint64_t reference = runAndDigest(500, 100, 7u);
    for (int trial = 0; trial < 10; ++trial) {
        CHECK(runAndDigest(500, 100, 7u) == reference);
    }
}

TEST_CASE("simulation runs headless with no window initialised") {
    // If any raylib call remains in the tick path this either crashes or
    // returns garbage, because no GL context or window exists here.
    Simulation sim(1280, 720, 99u);
    sim.init(1000);
    sim.setPaused(false);
    for (int i = 0; i < 50; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    CHECK(sim.getAgentCount() > 0);
    CHECK(sim.stateDigest() != 0ull);
}

namespace {
uint64_t runWithThreads(uint32_t threads, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    for (int i = 0; i < 200; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("state is identical regardless of worker thread count") {
    const uint64_t single = runWithThreads(1u);
    CHECK(runWithThreads(2u)  == single);
    CHECK(runWithThreads(4u)  == single);
    CHECK(runWithThreads(7u)  == single);
    CHECK(runWithThreads(16u) == single);
}

TEST_CASE("thread invariance holds across repeated trials") {
    const uint64_t reference = runWithThreads(1u, 5u);
    for (int trial = 0; trial < 5; ++trial) {
        CHECK(runWithThreads(8u, 5u) == reference);
    }
}

TEST_CASE("threads=0 (auto) matches an explicit hardware_concurrency()-1") {
    // threads=0 is the configuration the published README benchmark numbers were produced
    // under (JobSystem's "auto" path), yet nothing previously pinned it against an explicit
    // count. This nails down that auto resolves to exactly hardware_concurrency()-1 workers
    // (or 1, on a machine hardware_concurrency() can't characterize) and that its result is
    // bit-identical to requesting that count explicitly.
    const unsigned hc = std::thread::hardware_concurrency();
    const uint32_t explicitWorkers = hc > 1 ? hc - 1 : 1u;
    CHECK(runWithThreads(0u) == runWithThreads(explicitWorkers));
}
