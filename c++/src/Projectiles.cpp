#include "Projectiles.hpp"
#include "DetMath.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Rng.hpp"
#include <cmath>

// Behaviour arrives in tasks 8 and 9. This translation unit exists now so the
// CMake wiring and the tactix_sim raylib-free guarantee are settled before any
// logic depends on them.

void spawnArrows(const SoldierHot& soldiers, const SquadHot& squads,
                 ProjectileHot& out, const Rng& rng) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (!soldiers.intentFire[i]) continue;
        // Resolution steps 1-2 (melee, then projectile hits) can zero this
        // soldier's health earlier in the SAME tick, but state is not set to
        // Dead until recordCasualties (step 4), which runs after this. Without
        // this guard a soldier killed this tick still looses an arrow here --
        // matches the same guard in applyMeleeIntents (Combat.cpp).
        if (soldiers.health[i] == 0) continue;

        const uint16_t sq = soldiers.squadId[i];
        if (sq >= squads.count) continue;
        const uint32_t t = squads.targetSoldier[sq];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;

        const float px = soldiers.posX[i];
        const float py = soldiers.posY[i];

        // Lead the target by its own flight time, so a moving target is aimed
        // where it will be rather than where it is.
        float dx = soldiers.posX[t] - px;
        float dy = soldiers.posY[t] - py;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float flight = dist / kArrowSpeed;
        dx += soldiers.velX[t] * flight;
        dy += soldiers.velY[t] * flight;

        const float aimLen = std::sqrt(dx * dx + dy * dy);
        if (aimLen < 1e-4f) continue;
        float ax = dx / aimLen;
        float ay = dy / aimLen;

        // Accuracy degrades with range and with the archer moving. Integer
        // milliradians throughout: Rng::range takes ints.
        const float shooterSpeed = std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                                             soldiers.velY[i] * soldiers.velY[i]);
        const float maxRange = kUnitStats[(int)UnitType::Archer].range;
        const float maxSpeed = kUnitStats[(int)UnitType::Archer].speed;
        int spreadMrad = (int)((float)kArrowBaseSpreadMrad
                             * (1.0f + dist / maxRange)
                             * (1.0f + shooterSpeed / maxSpeed));
        if (spreadMrad < 1) spreadMrad = 1;

        const int offMrad = rng.range((uint32_t)i, RngUse::ArrowSpread,
                                      -spreadMrad, spreadMrad);
        const float theta = (float)offMrad * 0.001f;

        // detmath, not libm: libm's sin is not bit-identical across platforms
        // and this value feeds the state digest.
        const float st = detmath::sin(theta);
        const float ct = detmath::sin(theta + detmath::HALF_PI);  // cos
        const float rx = ax * ct - ay * st;
        const float ry = ax * st + ay * ct;

        out.spawn(px, py, rx * kArrowSpeed, ry * kArrowSpeed,
                  soldiers.team[i], kArrowDamage, kArrowLifetime);
    }
}

bool segmentHitsCircle(float x0, float y0, float x1, float y1,
                       float cx, float cy, float r) {
    const float sx = x1 - x0;
    const float sy = y1 - y0;
    const float lenSq = sx * sx + sy * sy;

    float t = 0.0f;
    if (lenSq > 1e-12f) {
        // Project the centre onto the segment, clamped to its ends.
        t = ((cx - x0) * sx + (cy - y0) * sy) / lenSq;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    const float nearestX = x0 + sx * t;
    const float nearestY = y0 + sy * t;
    const float dx = cx - nearestX;
    const float dy = cy - nearestY;
    return dx * dx + dy * dy <= r * r;
}

void integrateProjectile(ProjectileHot& p, const SoldierHot& soldiers,
                         const SpatialHash& hash, size_t i, float dt,
                         std::vector<uint32_t>& scratch) {
    p.intentHitTarget[i] = UINT32_MAX;
    p.lifetime[i] -= dt;
    if (p.lifetime[i] <= 0.0f) return;

    const float x0 = p.posX[i];
    const float y0 = p.posY[i];
    const float x1 = x0 + p.velX[i] * dt;
    const float y1 = y0 + p.velY[i] * dt;
    p.posX[i] = x1;
    p.posY[i] = y1;

    // Query around the segment's midpoint with a radius covering half its
    // length plus the soldier radius, so nothing along the path is missed.
    // NOTE: SpatialHash::queryNeighbors ignores its radius argument and
    // always returns the fixed 3x3 cell block (150px across at 50px cells)
    // around the position -- see the NOTE on its definition. That block is
    // far wider than an arrow's ~3.3px per-tick segment, so the candidate
    // set here is a superset of what `half + kSoldierRadius` would select,
    // never a subset; the per-candidate segmentHitsCircle check below still
    // filters correctly regardless.
    const float midX = (x0 + x1) * 0.5f;
    const float midY = (y0 + y1) * 0.5f;
    const float half = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) * 0.5f;
    hash.queryNeighbors(midX, midY, half + kSoldierRadius, scratch);

    uint32_t best = UINT32_MAX;
    for (uint32_t n : scratch) {
        if (soldiers.team[n] == p.team[i]) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;
        if (!segmentHitsCircle(x0, y0, x1, y1,
                               soldiers.posX[n], soldiers.posY[n], kSoldierRadius)) {
            continue;
        }
        // Lowest index wins any tie, so the outcome does not depend on the
        // order the spatial hash happened to return candidates in.
        if (n < best) best = n;
    }
    p.intentHitTarget[i] = best;
}

void applyProjectileHits(ProjectileHot& p, SoldierHot& soldiers) {
    for (size_t i = 0; i < p.count; ++i) {
        const uint32_t t = p.intentHitTarget[i];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;
        if (soldiers.health[t] == 0) continue;  // no underflow, no overkill

        soldiers.health[t] = (soldiers.health[t] > p.damage[i])
                           ? (uint8_t)(soldiers.health[t] - p.damage[i])
                           : (uint8_t)0;

        // An arrow that lands is spent. Marking it expired lets one compaction
        // pass remove both hits and misses that ran out of flight time.
        p.lifetime[i] = 0.0f;
        p.intentHitTarget[i] = UINT32_MAX;
    }
}

void compactProjectiles(ProjectileHot& p) {
    size_t i = 0;
    while (i < p.count) {
        if (p.lifetime[i] > 0.0f) {
            ++i;
            continue;
        }
        const size_t last = p.count - 1;
        if (i != last) {
            p.posX[i] = p.posX[last];
            p.posY[i] = p.posY[last];
            p.velX[i] = p.velX[last];
            p.velY[i] = p.velY[last];
            p.team[i] = p.team[last];
            p.damage[i] = p.damage[last];
            p.lifetime[i] = p.lifetime[last];
            p.intentHitTarget[i] = p.intentHitTarget[last];
            // Do not advance i: the entry swapped in is unexamined.
        } else {
            ++i;
        }
        p.posX.pop_back();
        p.posY.pop_back();
        p.velX.pop_back();
        p.velY.pop_back();
        p.team.pop_back();
        p.damage.pop_back();
        p.lifetime.pop_back();
        p.intentHitTarget.pop_back();
        p.count--;
    }
}
