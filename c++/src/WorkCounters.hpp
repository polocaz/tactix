#pragma once
#include <atomic>
#include <cstdint>

// Deterministic measures of work performed, independent of wall-clock time
// and of the machine running the code. CI gates on these; it never gates on
// timings, because hosted runners are too noisy for a timing threshold to be
// anything but a flake generator.
//
// Counters are accumulated atomically because the tick phases run on worker
// threads. Only the TOTAL is asserted on, never per-thread splits, so
// relaxed ordering is sufficient and the total stays deterministic.
struct WorkCounters {
    std::atomic<uint64_t> candidatesExamined{0};  // neighbour candidates inspected
    std::atomic<uint64_t> cellsVisited{0};        // grid cells touched by queries
    std::atomic<uint64_t> gridInsertions{0};      // entities inserted into the grid
    std::atomic<uint64_t> jobsDispatched{0};      // jobs submitted to the job system

    void reset() {
        candidatesExamined.store(0, std::memory_order_relaxed);
        cellsVisited.store(0, std::memory_order_relaxed);
        gridInsertions.store(0, std::memory_order_relaxed);
        jobsDispatched.store(0, std::memory_order_relaxed);
    }

    void add(std::atomic<uint64_t>& field, uint64_t n) {
        field.fetch_add(n, std::memory_order_relaxed);
    }
};
