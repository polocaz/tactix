#pragma once
#include "Units.hpp"
#include "Squads.hpp"
#include "Rng.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;  // defined in Simulation.hpp

// Arrows in flight. A third SoA array alongside SoldierHot and SquadHot, with
// a very different lifetime pattern: entries are created and destroyed
// constantly rather than persisting for the run.
struct ProjectileHot {
    std::vector<float>    posX;
    std::vector<float>    posY;
    std::vector<float>    velX;
    std::vector<float>    velY;
    std::vector<Team>     team;
    std::vector<uint8_t>  damage;
    std::vector<float>    lifetime;

    // Written by phase 5, consumed by resolution, mirroring the soldier intent
    // pattern. UINT32_MAX means this arrow hit nothing this tick.
    std::vector<uint32_t> intentHitTarget;

    size_t count = 0;

    void spawn(float px, float py, float vx, float vy, Team t, uint8_t dmg, float life) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        team.push_back(t);
        damage.push_back(dmg);
        lifetime.push_back(life);
        intentHitTarget.push_back(UINT32_MAX);
        count++;
    }

    void clear() {
        posX.clear();
        posY.clear();
        velX.clear();
        velY.clear();
        team.clear();
        damage.clear();
        lifetime.clear();
        intentHitTarget.clear();
        count = 0;
    }
};

// Resolution step 3 (spec 5.5). Converts intentFire flags into arrows, in
// ascending soldier index order so the projectile array's contents do not
// depend on thread scheduling.
void spawnArrows(const SoldierHot& soldiers, const SquadHot& squads,
                 ProjectileHot& out, const Rng& rng);
