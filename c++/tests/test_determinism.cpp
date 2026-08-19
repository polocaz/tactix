#include <doctest/doctest.h>
#include "Simulation.hpp"

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
