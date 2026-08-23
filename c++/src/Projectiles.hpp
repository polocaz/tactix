#pragma once
#include "Units.hpp"
#include "Loadout.hpp"
#include "Squads.hpp"
#include "Rng.hpp"
#include "SpatialHash.hpp"
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
    // What threw this, as a WeaponClass byte. Carried on the ARROW rather than
    // looked up from the shooter, because by the time it lands the shooter may
    // be dead and compaction has renumbered every survivor: his index now names
    // someone else entirely.
    std::vector<uint8_t>  weapon;
    std::vector<float>    lifetime;

    // Written by phase 5, consumed by resolution, mirroring the soldier intent
    // pattern. UINT32_MAX means this arrow hit nothing this tick.
    // Arc model (design 8.1). An arrow is above head height until it has flown
    // `liveAfter` px, and hit-tests nothing until then. Once live it can hit
    // EITHER team: your own screen is under the arc and safe, but volleying
    // into a mixed melee kills your own men.
    std::vector<float> traveled;
    std::vector<float> liveAfter;

    std::vector<uint32_t> intentHitTarget;

    size_t count = 0;

    // `w` defaults to Bow for the many tests that exercise flight, compaction
    // or arming and do not care what threw the arrow. Every production caller
    // passes it explicitly, and the javelin volley is covered by a test that
    // asserts on the weapon it comes out with, so the default cannot quietly
    // turn a pilum into an arrow.
    void spawn(float px, float py, float vx, float vy, Team t, uint8_t dmg,
               float life, float armAfter,
               uint8_t w = (uint8_t)WeaponClass::Bow) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        team.push_back(t);
        damage.push_back(dmg);
        weapon.push_back(w);
        lifetime.push_back(life);
        traveled.push_back(0.0f);
        liveAfter.push_back(armAfter);
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
        weapon.clear();
        lifetime.clear();
        traveled.clear();
        liveAfter.clear();
        intentHitTarget.clear();
        count = 0;
    }
};

// Resolution step 3 (spec 5.5). Converts intentFire flags into arrows, in
// ascending soldier index order so the projectile array's contents do not
// depend on thread scheduling.
void spawnArrows(const SoldierHot& soldiers, const SquadHot& squads,
                 ProjectileHot& out, const Rng& rng);

// Closest-approach test between a segment and a circle. Used instead of
// sampling the arrow's endpoint each tick, because tunnelling depends on
// RELATIVE velocity, not arrow speed alone: an arrow at 200px/s against
// cavalry crossing at 95px/s covers about 4.9px of relative displacement per
// tick against a 4px radius, so a point test would let arrows pass straight
// through a galloping target.
bool segmentHitsCircle(float x0, float y0, float x1, float y1,
                       float cx, float cy, float r);

// Phase 5 (spec 5.4 step 5). Moves one arrow by dt and writes its
// intentHitTarget for resolution to consume. Parallel-safe: touches only
// projectile index i and reads soldiers, never writes them.
void integrateProjectile(ProjectileHot& p, const SoldierHot& soldiers,
                         const SpatialHash& hash, size_t i, float dt);

// Resolution step 2 (spec 5.5). An arrow whose flight crossed a soldier rolls
// against what he is wearing (kWoundChancePct, indexed by the arrow's own
// weapon), then the arrow is marked spent (expired) whether or not it wounded,
// so a single compaction pass removes both those and the arrows that ran out of
// flight time.
//
// `squads` is non-const because a later commit accumulates missile pressure on
// the struck man's squad from here.
// Must run before spawnArrows so an arrow spawned this tick is not
// hit-tested before it has flown, and before compactDead, because
// intentHitTarget holds soldier indices that compaction invalidates.
void applyProjectileHits(ProjectileHot& p, SoldierHot& soldiers, SquadHot& squads,
                         const Rng& rng);

// Removes every expired or spent arrow (lifetime <= 0), swap-with-back so
// the surviving arrows stay contiguous. Must run after applyProjectileHits
// and before compactDead for the same soldier-index-invalidation reason.
void compactProjectiles(ProjectileHot& p);
