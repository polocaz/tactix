#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "WorkCounters.hpp"

#include <fstream>
#include <map>
#include <string>

namespace {
struct CounterSnapshot {
    uint64_t candidatesExamined, cellsVisited, gridInsertions, jobsDispatched, squadDecisions;
    uint64_t stateDigest;
};

CounterSnapshot snapshot(const WorkCounters& c, uint64_t digest) {
    return { c.candidatesExamined.load(), c.cellsVisited.load(),   c.gridInsertions.load(),
             c.jobsDispatched.load(),     c.squadDecisions.load(), digest };
}

CounterSnapshot runAndCount(uint32_t threads, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    sim.resetCounters();
    for (int i = 0; i < 200; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return snapshot(sim.counters(), sim.stateDigest());
}

// Deliberately a flat key=value file, not JSON: gating needs five values,
// and adding a JSON parser to read five values would be silly.
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
    const CounterSnapshot c = runAndCount(1u);
    CHECK(c.candidatesExamined > 0ull);
    CHECK(c.cellsVisited > 0ull);
    CHECK(c.gridInsertions > 0ull);
    CHECK(c.jobsDispatched > 0ull);
    CHECK(c.squadDecisions > 0ull);
}

TEST_CASE("counters are identical regardless of thread count") {
    const CounterSnapshot single = runAndCount(1u);
    const CounterSnapshot many   = runAndCount(8u);
    CHECK(single.candidatesExamined == many.candidatesExamined);
    CHECK(single.cellsVisited       == many.cellsVisited);
    CHECK(single.gridInsertions     == many.gridInsertions);
    CHECK(single.jobsDispatched     == many.jobsDispatched);
    CHECK(single.squadDecisions     == many.squadDecisions);
    CHECK(single.stateDigest        == many.stateDigest);
}

TEST_CASE("counters reproduce across runs") {
    const CounterSnapshot a = runAndCount(4u);
    const CounterSnapshot b = runAndCount(4u);
    CHECK(a.candidatesExamined == b.candidatesExamined);
    CHECK(a.cellsVisited       == b.cellsVisited);
    CHECK(a.gridInsertions     == b.gridInsertions);
    CHECK(a.jobsDispatched     == b.jobsDispatched);
    CHECK(a.squadDecisions     == b.squadDecisions);
    CHECK(a.stateDigest        == b.stateDigest);
}

// The committed baseline (c++/tests/baseline/counters-2k-200.txt) is the
// single source of truth for exact counter/digest values -- see that file's
// header for how to regenerate it. An intentional optimisation SHOULD
// change these numbers; update the baseline file in the same commit.
TEST_CASE("work counters and state digest match the committed baseline") {
    const auto expected = loadBaseline(std::string(TACTIX_BASELINE_DIR) + "/counters-2k-200.txt");
    const CounterSnapshot actual = runAndCount(1u);

    CHECK(actual.candidatesExamined == requireKey(expected, "candidatesExamined"));
    CHECK(actual.cellsVisited       == requireKey(expected, "cellsVisited"));
    CHECK(actual.gridInsertions     == requireKey(expected, "gridInsertions"));
    CHECK(actual.jobsDispatched     == requireKey(expected, "jobsDispatched"));
    CHECK(actual.squadDecisions     == requireKey(expected, "squadDecisions"));
    CHECK(actual.stateDigest        == requireKey(expected, "stateDigest"));
}
