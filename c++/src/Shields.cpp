#include "Shields.hpp"
#include "Formation.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include <algorithm>
#include <cmath>

ImpactArc impactArc(float impactDirX, float impactDirY,
                    float facingX, float facingY) {
    const float len = std::sqrt(impactDirX * impactDirX + impactDirY * impactDirY);
    if (len < 1e-6f) return ImpactArc::Front;   // degenerate, treat as head on

    // Negated: a blow travelling the same way a man faces is one coming at him
    // from behind. cos == 1 is straight into the face.
    const float cos = -((impactDirX * facingX + impactDirY * facingY) / len);
    if (cos >  0.5f) return ImpactArc::Front;
    if (cos > -0.5f) return ImpactArc::Side;
    return ImpactArc::Rear;
}

uint8_t blockChancePct(ShieldClass shield, FormationShape shape,
                       ImpactArc arc, bool melee) {
    // No shield blocks nothing, whatever formation he is standing in. The
    // early return is what makes formation cover a bonus ON a shield rather
    // than a substitute for one.
    if (shield == ShieldClass::None) return 0;

    const float base = (float)kShieldCoverPct[(int)shield] * kArcCoverScale[(int)arc];
    float pct = base + (float)traitsOf(shape).cover[(int)arc];
    if (melee) pct *= kShieldMeleeScale;

    if (pct < 0.0f) return 0;
    if (pct > (float)kMaxBlockPct) return kMaxBlockPct;
    return (uint8_t)pct;
}

uint8_t shieldBlockPct(const SoldierHot& soldiers, const SquadHot& squads,
                       uint32_t target, float impactDirX, float impactDirY,
                       bool melee) {
    const uint16_t s = soldiers.squadId[target];
    if ((size_t)s >= squads.count) return 0;

    const ShieldClass shield = loadoutOf(soldiers.troopClass[target]).shield;
    const ImpactArc arc = impactArc(impactDirX, impactDirY,
                                    squads.facingX[s], squads.facingY[s]);

    const uint8_t now = blockChancePct(shield, (FormationShape)squads.shape[s], arc, melee);
    if (squads.shapeBlend[s] <= 0.0f) return now;

    // Mid-drill a squad has neither shape's cover properly. Taking the worse of
    // the two is the entire cost of changing formation under fire, and without
    // it a squad would get a testudo's protection the instant it decided to
    // form one.
    const uint8_t was = blockChancePct(shield, (FormationShape)squads.prevShape[s], arc, melee);
    return std::min(now, was);
}
