#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "WorkCounters.hpp"

namespace {
struct CounterSnapshot {
    uint64_t candidatesExamined, cellsVisited, gridInsertions, jobsDispatched;
};

CounterSnapshot snapshot(const WorkCounters& c) {
    return { c.candidatesExamined.load(), c.cellsVisited.load(),
             c.gridInsertions.load(),     c.jobsDispatched.load() };
}

CounterSnapshot runAndCount(uint32_t threads, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    sim.resetCounters();
    for (int i = 0; i < 200; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return snapshot(sim.counters());
}
} // namespace

TEST_CASE("counters are non-zero for a real run") {
    const CounterSnapshot c = runAndCount(1u);
    CHECK(c.candidatesExamined > 0ull);
    CHECK(c.cellsVisited > 0ull);
    CHECK(c.gridInsertions > 0ull);
    CHECK(c.jobsDispatched > 0ull);
}

TEST_CASE("grid insertions equal the exact observed total") {
    // entities.count shrinks across the run (Simulation.cpp swap-removes dead
    // agents), so this is not simply 2000 * 200. Value observed empirically
    // at seed 42 for 2000 agents over 200 ticks; see task-8-report.md.
    const CounterSnapshot c = runAndCount(1u);
    CHECK(c.gridInsertions == 399467ull);
}

TEST_CASE("counters are identical regardless of thread count") {
    const CounterSnapshot single = runAndCount(1u);
    const CounterSnapshot many   = runAndCount(8u);
    CHECK(single.candidatesExamined == many.candidatesExamined);
    CHECK(single.cellsVisited       == many.cellsVisited);
    CHECK(single.gridInsertions     == many.gridInsertions);
    CHECK(single.jobsDispatched     == many.jobsDispatched);
}

TEST_CASE("counters reproduce across runs") {
    const CounterSnapshot a = runAndCount(4u);
    const CounterSnapshot b = runAndCount(4u);
    CHECK(a.candidatesExamined == b.candidatesExamined);
    CHECK(a.cellsVisited       == b.cellsVisited);
    CHECK(a.gridInsertions     == b.gridInsertions);
    CHECK(a.jobsDispatched     == b.jobsDispatched);
}
