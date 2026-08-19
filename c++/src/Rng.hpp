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
    SeparationPushX,        // pushed out of a building
    SeparationPushY,
    SeparationTreePushX,    // pushed out of a tree (reachable in the same
    SeparationTreePushY,    // agent-tick as the building push, so distinct)

    // init(): civilian spawned next to a building
    SpawnNearBuildingOffsetX,
    SpawnNearBuildingOffsetY,
    // init(): zombies in the graveyard
    SpawnZombiePosX,
    SpawnZombiePosY,
    SpawnZombieVelX,
    SpawnZombieVelY,
    // init(): heroes along the top
    SpawnHeroPosX,
    SpawnHeroPosY,
    SpawnHeroVelX,
    SpawnHeroVelY,

    // setAgentCount(): agents appended after startup
    AddCivilianPosX,
    AddCivilianPosY,
    AddCivilianVelX,
    AddCivilianVelY,
    AddZombiePosX,
    AddZombiePosY,
    AddZombieVelX,
    AddZombieVelY,
    AddHeroPosX,
    AddHeroPosY,
    AddHeroVelX,
    AddHeroVelY,

    // updateBehaviorsChunk()
    FleeStrategyChoice,     // panic vs. run to a hero
    HeroAimDelay,           // 0.3-0.6s before the shot
    PatrolRetargetX,        // new patrol destination on arrival
    PatrolRetargetY,

    // updateInfections()
    InfectionDeathReanimationDelay,  // corpse timer after dying of a bite
    ReanimateVelX,                   // kick given to a freshly risen zombie
    ReanimateVelY,
    CombatDurationHero,              // 1-2s when the victim is a hero
    CombatDurationCivilian,          // 2-4s otherwise

    // resolveCivilianVsZombieCombat()
    CivilianCombatRoll,              // 0-99 outcome roll
    PyrrhicInfectionDuration,        // killed the zombie but was bitten
    BittenEscapeInfectionDuration,   // escaped but was bitten
    CombatDeathReanimationDelay,     // corpse timer after losing the fight

    // resolveHeroVsZombieCombat()
    HeroCombatRoll,                  // 0-99 outcome roll

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
