#pragma once
#include <chrono>
#include <cstdint>

// Where one tick's wall-clock time went, phase by phase.
//
// Measurement only. Nothing in the simulation reads a profile, no phase
// behaves differently while one is being taken, and no timing enters
// stateDigest. That is what makes a profiled run and an unprofiled run
// comparable to each other at all, and test_determinism.cpp asserts it rather
// than leaving it to the reader.
//
// This is the answer to Phase B of the roadmap, and it replaces a worse
// method. The tick breakdown published before this existed was produced by
// editing a phase out of the source by hand, rebuilding, and diffing the
// resulting p50 -- which measures the right thing but is not a command anyone
// else can run, and quietly changes the simulation while it measures it.
struct TickProfile {
    // In the order Simulation::tick runs them. The marks partition the tick
    // with no gaps: every nanosecond between the first and the last is charged
    // to exactly one phase, so the entries sum to the tick.
    enum Phase : uint8_t {
        SnapshotPrev = 0,  // copy pos into prevPos for interpolation (serial)
        SpatialHash,       // rebuildSpatialHash + rebuildInfluence (serial)
        SquadAggregate,    // parallel over squads, incl. contact detection
        ArmyDecide,        // serial, two entities
        SquadDecide,       // parallel over squads
        SoldierSteer,      // parallel over soldiers: the neighbour walk
        Projectiles,       // parallel over projectiles
        Resolution,        // serial: the only cross-agent mutation
        Movement,          // parallel over soldiers, writes nextPos
        Contact,           // parallel over soldiers: non-penetration
        ClampToWorld,      // serial
        kCount
    };

    double ms[kCount] = {};

    // Stable, machine-readable names. Used as JSON keys by the benchmark, so
    // they are lowerCamelCase like every other key it emits, and changing one
    // breaks whatever is parsing that output.
    static const char* name(Phase p) {
        switch (p) {
            case SnapshotPrev:   return "snapshotPrev";
            case SpatialHash:    return "spatialHash";
            case SquadAggregate: return "squadAggregate";
            case ArmyDecide:     return "armyDecide";
            case SquadDecide:    return "squadDecide";
            case SoldierSteer:   return "soldierSteer";
            case Projectiles:    return "projectiles";
            case Resolution:     return "resolution";
            case Movement:       return "movement";
            case Contact:        return "contact";
            case ClampToWorld:   return "clampToWorld";
            default:             return "unknown";
        }
    }
};

// Splits a tick into per-phase costs. mark() closes the phase that just ran
// and opens the next one, so a phase's cost is the time since the previous
// mark, and the barrier a parallel phase waits on is charged to that phase
// rather than to the one after it.
//
// Disabled is the default and costs one predictable branch per mark, eleven
// per tick. Enabled costs eleven steady_clock reads per tick on top of that,
// which is roughly 0.0003 ms against a tick of about 4 ms.
//
// That estimate is not the reason to trust the numbers; the measurement is.
// Three profiled and three unprofiled runs at `--agents 10000 --ticks 2000`
// gave p50s of 3.79/3.81/3.91 ms profiled against 4.08/4.01/3.95 ms
// unprofiled, so the overhead does not resolve above the run-to-run noise on
// this machine and the profiled run was in fact the faster of the two sets.
// What that supports is only "too small to see here", NOT "zero": on a
// platform with an expensive clock, or at a tick count where 11 reads matter,
// re-measure rather than quoting this.
class PhaseClock {
public:
    PhaseClock(bool enabled, TickProfile& out) : on(enabled), profile(out) {
        if (on) last = std::chrono::steady_clock::now();
    }

    void mark(TickProfile::Phase p) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        profile.ms[p] = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
    }

private:
    bool on;
    TickProfile& profile;
    std::chrono::steady_clock::time_point last;
};
