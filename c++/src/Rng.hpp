#pragma once
#include <cstdint>

// Identifies WHICH random draw is being made. Two draws for the same agent on
// the same tick must never share an enumerator, or they return the same value.
//
// Rule when replacing a GetRandomValue call site: add an enumerator named for
// what the draw produces. Never reuse one unless the two sites are provably
// unreachable within the same agent-tick.
enum class RngUse : uint32_t {
    SpawnPosX = 1,
    SpawnPosY,
    SpawnVelX,
    SpawnVelY,
    SpawnPatrolX,
    SpawnPatrolY,
    SpawnHeroType,
    ObstacleBuildingX,
    ObstacleBuildingY,
    ObstacleBuildingW,
    ObstacleBuildingH,
    ObstacleTreeX,
    ObstacleTreeY,
    ObstacleTreeRadius,
    SeparationPushX,
    SeparationPushY,
    // Add one enumerator per remaining call site during Task 5.
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

    // [0.0f, 1.0f). Top 24 bits scaled by 2^-24 — exact in float.
    float unit(uint32_t agentIndex, RngUse use) const {
        return static_cast<float>(bits(agentIndex, use) >> 8) * 0x1.0p-24f;
    }
};
