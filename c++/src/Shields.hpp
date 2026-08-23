#pragma once
#include "Loadout.hpp"
#include "Units.hpp"
#include <cstdint>

struct SoldierHot;
struct SquadHot;

// Which side of a man a blow arrived on. Front is a 120 degree arc, and so are
// the other two: even thirds are the honest default and nothing in the design
// argues for skewing them.
enum class ImpactArc : uint8_t { Front = 0, Side = 1, Rear = 2 };

// (impactDirX, impactDirY) is the direction the blow TRAVELS, not the direction
// it came from. Neither vector needs to be normalized: the impact direction is
// divided by its own length here, and facing is already unit length by the
// invariant normalizeFacing maintains.
ImpactArc impactArc(float impactDirX, float impactDirY,
                    float facingX, float facingY);

// The whole shield model, as a pure function of four small values. Formation
// cover is a bonus ON a shield rather than a substitute for one: a man with no
// shield blocks nothing however he is standing, which is why the arc scale
// multiplies before the formation bonus adds, and why the add cannot rescue a
// zero.
uint8_t blockChancePct(ShieldClass shield, FormationShape shape,
                       ImpactArc arc, bool melee);

// Thin wrapper for the two resolution sites. Uses the target's SQUAD facing
// rather than his own dirX/dirY: a man standing still in a fight keeps whatever
// direction he was last moving in, which is stale and sometimes meaningless,
// while the squad facing is the deliberate statement of where the formation
// points and is what the shields of a formed body actually follow.
//
// While a squad is mid-transition (shapeBlend > 0) this takes the WORSE of its
// old and new shapes, per arc. That is the cost of drilling under fire.
uint8_t shieldBlockPct(const SoldierHot& soldiers, const SquadHot& squads,
                       uint32_t target, float impactDirX, float impactDirY,
                       bool melee);
