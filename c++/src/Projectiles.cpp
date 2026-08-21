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
