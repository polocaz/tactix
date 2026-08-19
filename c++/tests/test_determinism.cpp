#include <doctest/doctest.h>
#include "Simulation.hpp"

namespace {
// Runs a simulation to completion and returns the digest of its final state.
uint64_t runAndDigest(size_t agents, int ticks) {
    Simulation sim(1280, 720);
    sim.init(agents);
    sim.setPaused(false);
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("two identical runs produce identical state") {
    const uint64_t a = runAndDigest(2000, 200);
    const uint64_t b = runAndDigest(2000, 200);
    CHECK(a == b);
}
