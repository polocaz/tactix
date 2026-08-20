#include <doctest/doctest.h>
#include "Rng.hpp"
#include <set>

TEST_CASE("same inputs always produce the same value") {
    Rng a{42u, 100u};
    Rng b{42u, 100u};
    for (uint32_t i = 0; i < 1000; ++i) {
        CHECK(a.range(i, RngUse::SpawnPosX, 0, 1279) ==
              b.range(i, RngUse::SpawnPosX, 0, 1279));
    }
}

TEST_CASE("different seeds produce different streams") {
    Rng a{1u, 0u};
    Rng b{2u, 0u};
    int differences = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (a.range(i, RngUse::SpawnPosX, 0, 1279) !=
            b.range(i, RngUse::SpawnPosX, 0, 1279)) {
            ++differences;
        }
    }
    CHECK(differences > 900);   // near-total divergence expected
}

TEST_CASE("different call sites on the same agent-tick do not correlate") {
    Rng r{42u, 7u};
    int collisions = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (r.range(i, RngUse::SpawnPosX, 0, 1279) ==
            r.range(i, RngUse::SpawnPosY, 0, 1279)) {
            ++collisions;
        }
    }
    CHECK(collisions < 50);   // ~1/1280 expected by chance
}

TEST_CASE("different ticks produce different streams") {
    Rng t0{42u, 0u};
    Rng t1{42u, 1u};
    int differences = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (t0.range(i, RngUse::SpawnPosX, 0, 1279) !=
            t1.range(i, RngUse::SpawnPosX, 0, 1279)) {
            ++differences;
        }
    }
    CHECK(differences > 900);
}

TEST_CASE("range is inclusive on both ends, like GetRandomValue") {
    Rng r{42u, 0u};
    std::set<int> seen;
    for (uint32_t i = 0; i < 20000; ++i) {
        int v = r.range(i, RngUse::SpawnVelX, -2, 2);
        CHECK(v >= -2);
        CHECK(v <= 2);
        seen.insert(v);
    }
    CHECK(seen.size() == 5);   // all of -2,-1,0,1,2 must occur
}

TEST_CASE("range handles a single-value span") {
    Rng r{42u, 0u};
    CHECK(r.range(0u, RngUse::SpawnVelX, 5, 5) == 5);
}

// The RngUse enum's own comment promises pairwise distinctness across ALL ~55 enumerators
// ("Two draws for the same agent on the same tick must never share an enumerator, or they
// return the same value"), not just the one SpawnPosX/SpawnPosY pair the earlier test above
// checks. An implementation that ignored `use` for 53 of 55 enumerators (e.g. accidentally
// hashing only a handful of distinct bit patterns) would still pass that earlier test.
TEST_CASE("no two RngUse enumerators collide, for any agent/tick") {
    const uint32_t useCount = static_cast<uint32_t>(RngUse::Count) - 1;  // exclude the sentinel
    int collisions = 0;
    int totalPairs = 0;
    for (uint32_t agentIndex : {0u, 1u, 7u, 999u}) {
        Rng r{42u, 3u};
        for (uint32_t a = 1; a <= useCount; ++a) {
            for (uint32_t b = a + 1; b <= useCount; ++b) {
                ++totalPairs;
                if (r.bits(agentIndex, static_cast<RngUse>(a)) ==
                    r.bits(agentIndex, static_cast<RngUse>(b))) {
                    ++collisions;
                }
            }
        }
    }
    CHECK(totalPairs > 0);
    CHECK(collisions == 0);
}
