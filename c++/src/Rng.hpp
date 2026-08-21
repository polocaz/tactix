#pragma once
#include <cstdint>

// Identifies WHICH random draw is being made. Two draws for the same agent on
// the same tick must never share an enumerator, or they return the same value.
//
// Rule when replacing a GetRandomValue call site: add an enumerator named for
// what the draw produces. Never reuse one unless the two sites are provably
// unreachable within the same agent-tick.
//
// Enumerator VALUES are hashed directly into bits() below, so they are part
// of every draw's input and, transitively, of stateDigest(). Never reorder
// or delete an enumerator, including ones with no production reader today
// (e.g. SpawnPosX/Y, SpawnVelX/Y) -- doing so silently reshuffles the values
// of every enumerator declared after it and moves the digest for no reason.
// Only ever append a new one immediately before Count.
enum class RngUse : uint32_t {
    SpawnPosX = 1,
    SpawnPosY,
    SpawnVelX,
    SpawnVelY,
    ObstacleBuildingX,
    ObstacleBuildingY,
    ObstacleBuildingW,
    ObstacleBuildingH,
    ObstacleTreeX,
    ObstacleTreeY,
    ObstacleTreeRadius,
    SeparationPushX,        // pushed out of a building
    SeparationPushY,
    SeparationTreePushX,    // pushed out of a tree (reachable in the same
    SeparationTreePushY,    // agent-tick as the building push, so distinct)

    // Deployment
    DeployJitterX,
    DeployJitterY,

    // Plan 2: archery
    ArrowSpread,

    // Keep this trailing sentinel last.
    Count
};

// PCG-derived integer hash (O'Neill). Good avalanche, no state.
inline uint32_t pcgHash(uint32_t x) {
    uint32_t state = x * 747796405u + 2891336453u;
    uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

struct Rng {
    uint32_t seed = 1u;
    uint32_t tick = 0u;

    uint32_t bits(uint32_t agentIndex, RngUse use) const {
        // Odd multipliers keep the three inputs from aliasing under XOR.
        return pcgHash(seed
                     ^ (agentIndex * 0x9E3779B9u)
                     ^ (tick * 0x85EBCA6Bu)
                     ^ (static_cast<uint32_t>(use) * 0xC2B2AE35u));
    }

    // Inclusive [lo, hi] — identical semantics to raylib's GetRandomValue,
    // so replacing call sites does not change the range of behaviour.
    int range(uint32_t agentIndex, RngUse use, int lo, int hi) const {
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(bits(agentIndex, use) % span);
    }
};
