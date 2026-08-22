#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "WorkCounters.hpp"

#include <fstream>
#include <map>
#include <string>

namespace {
struct CounterSnapshot {
    uint64_t candidatesExamined, cellsVisited, gridInsertions, jobsDispatched, squadDecisions;
    uint64_t projectileHitTests;
    uint64_t stateDigest;
};

CounterSnapshot snapshot(const WorkCounters& c, uint64_t digest) {
    return { c.candidatesExamined.load(),  c.cellsVisited.load(),      c.gridInsertions.load(),
             c.jobsDispatched.load(),      c.squadDecisions.load(),    c.projectileHitTests.load(),
             digest };
}

CounterSnapshot runAndCount(uint32_t threads, uint32_t seed = 42u, int ticks = 200) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    sim.resetCounters();
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return snapshot(sim.counters(), sim.stateDigest());
}

// Deliberately a flat key=value file, not JSON: gating needs six values,
// and adding a JSON parser to read six values would be silly.
//
// stateDigest is hex (matches tactix_bench --json's "%016llx" output
// verbatim, so it copy-pastes with no mental conversion); every other key
// is decimal. std::stoull silently stops at the first character it can't
// parse rather than throwing -- parsing a hex digest as base 10 returns
// its leading decimal-looking prefix instead of failing -- so every value
// is checked for full consumption, not just successful parsing.
std::map<std::string, uint64_t> loadBaseline(const std::string& path) {
    std::map<std::string, uint64_t> values;
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open baseline file: " << path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string valueStr = line.substr(eq + 1);
        const int base = (key == "stateDigest") ? 16 : 10;
        size_t consumed = 0;
        uint64_t value = 0;
        try {
            value = std::stoull(valueStr, &consumed, base);
        } catch (const std::exception& e) {
            REQUIRE_MESSAGE(false, "baseline key '" << key << "' has unparseable value '"
                                                      << valueStr << "': " << e.what());
        }
        REQUIRE_MESSAGE(consumed == valueStr.size(),
                         "baseline key '" << key << "' value '" << valueStr
                                           << "' was only partially parsed (base " << base
                                           << ") -- check for a stray character or wrong base");
        values[key] = value;
    }
    return values;
}

// map::at()'s std::out_of_range carries no context ("map::at"); this names
// the missing key so a malformed baseline line fails legibly instead of
// opaquely.
uint64_t requireKey(const std::map<std::string, uint64_t>& values, const std::string& key) {
    const auto it = values.find(key);
    REQUIRE_MESSAGE(it != values.end(), "baseline file is missing key '" << key << "'");
    return it->second;
}
} // namespace

TEST_CASE("counters are non-zero for a real run") {
    // 1600 ticks, not the default 200: at 200 ticks no archer has a legitimate
    // (cross-team, in-range) target yet -- see counters-2k-1600.txt's header --
    // so projectileHitTests would be 0 at 200 ticks now that squads no
    // longer acquire a same-team target on tick 1's transient.
    const CounterSnapshot c = runAndCount(1u, 42u, 1600);
    CHECK(c.candidatesExamined > 0ull);
    CHECK(c.cellsVisited > 0ull);
    CHECK(c.gridInsertions > 0ull);
    CHECK(c.jobsDispatched > 0ull);
    CHECK(c.squadDecisions > 0ull);
    CHECK(c.projectileHitTests > 0ull);
}

TEST_CASE("counters are identical regardless of thread count") {
    const CounterSnapshot single = runAndCount(1u);
    const CounterSnapshot many   = runAndCount(8u);
    CHECK(single.candidatesExamined  == many.candidatesExamined);
    CHECK(single.cellsVisited        == many.cellsVisited);
    CHECK(single.gridInsertions      == many.gridInsertions);
    CHECK(single.jobsDispatched      == many.jobsDispatched);
    CHECK(single.squadDecisions      == many.squadDecisions);
    CHECK(single.projectileHitTests  == many.projectileHitTests);
    CHECK(single.stateDigest         == many.stateDigest);
}

TEST_CASE("counters reproduce across runs") {
    const CounterSnapshot a = runAndCount(4u);
    const CounterSnapshot b = runAndCount(4u);
    CHECK(a.candidatesExamined  == b.candidatesExamined);
    CHECK(a.cellsVisited        == b.cellsVisited);
    CHECK(a.gridInsertions      == b.gridInsertions);
    CHECK(a.jobsDispatched      == b.jobsDispatched);
    CHECK(a.squadDecisions      == b.squadDecisions);
    CHECK(a.projectileHitTests  == b.projectileHitTests);
    CHECK(a.stateDigest         == b.stateDigest);
}

// The committed baseline (c++/tests/baseline/counters-2k-200.txt) is the
// single source of truth for exact counter/digest values -- see that file's
// header for how to regenerate it. An intentional optimisation SHOULD
// change these numbers; update the baseline file in the same commit.
TEST_CASE("work counters and state digest match the committed baseline") {
    const auto expected = loadBaseline(std::string(TACTIX_BASELINE_DIR) + "/counters-2k-200.txt");
    const CounterSnapshot actual = runAndCount(1u);

    CHECK(actual.candidatesExamined  == requireKey(expected, "candidatesExamined"));
    CHECK(actual.cellsVisited        == requireKey(expected, "cellsVisited"));
    CHECK(actual.gridInsertions      == requireKey(expected, "gridInsertions"));
    CHECK(actual.jobsDispatched      == requireKey(expected, "jobsDispatched"));
    CHECK(actual.squadDecisions      == requireKey(expected, "squadDecisions"));
    CHECK(actual.projectileHitTests  == requireKey(expected, "projectileHitTests"));
    CHECK(actual.stateDigest         == requireKey(expected, "stateDigest"));
}

// counters-2k-200.txt never reaches combat: at 2000 agents on the default
// field, no soldier has died by tick 200. It is the ONLY artifact comparing
// ubuntu and windows output, so without this second baseline, melee
// resolution, casualty recording, officer capture, and compactDead had NO
// cross-platform reference at all.
//
// The tick count here is NOT arbitrary and must be re-checked whenever
// anything changes how fast the armies close. It was 700, chosen when first
// death landed at tick 448. Non-penetration then slowed the approach by about
// a third (two dense crowds now physically resist each other), pushing first
// death past 1100 and leaving the 700-tick run covering no combat at all --
// which silently defeats the entire purpose of this baseline.
//
// 1600 is comfortably past first contact with margin for further tuning. If a
// later change slows the advance again, move this rather than accepting a
// green run: the tell is gridInsertions reading exactly agents * ticks, which
// means nobody died. See that file's header, same rules as counters-2k-200.txt.
TEST_CASE("work counters and state digest match the committed post-contact baseline") {
    const auto expected = loadBaseline(std::string(TACTIX_BASELINE_DIR) + "/counters-2k-1600.txt");
    const CounterSnapshot actual = runAndCount(1u, 42u, 1600);

    CHECK(actual.candidatesExamined  == requireKey(expected, "candidatesExamined"));
    CHECK(actual.cellsVisited        == requireKey(expected, "cellsVisited"));
    CHECK(actual.gridInsertions      == requireKey(expected, "gridInsertions"));
    CHECK(actual.jobsDispatched      == requireKey(expected, "jobsDispatched"));
    CHECK(actual.squadDecisions      == requireKey(expected, "squadDecisions"));
    CHECK(actual.projectileHitTests  == requireKey(expected, "projectileHitTests"));
    CHECK(actual.stateDigest         == requireKey(expected, "stateDigest"));
}
