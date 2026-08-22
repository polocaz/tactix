#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SquadHot;

// What the commander has told a squad to be. Appended, never renumbered:
// role reaches the state digest.
enum class SquadRole : uint8_t {
    Reserve = 0,   // hold behind the line, fill gaps
    Line    = 1,   // anchor the battle line, engage the assigned enemy
    Screen  = 2,   // stand between wardSquad and its nearest threat
    Flank   = 3,   // wide route to an assigned enemy's flank
    Shoot   = 4,   // hold a firing position behind the line
};

enum class ArmyPosture : uint8_t { Press = 0, Hold = 1, Fallback = 2 };

// The army tier: exactly two entries, one per team (design 7.1). Two entities
// make a serial decide phase free, and serial makes bit-reproducibility free.
struct ArmyHot {
    std::vector<float> strengthInfantry, strengthArcher, strengthCavalry;
    std::vector<float> centroidX, centroidY;

    // The battle line. frontX/Y is where this army's infantry stands;
    // frontDirX/Y points at the enemy army. Together they define "behind our
    // line", which is what archer positioning and reserve placement both need
    // and what no individual squad can work out on its own.
    std::vector<float> frontX, frontY, frontDirX, frontDirY;

    std::vector<uint8_t> posture;

    size_t count = 0;

    void spawn() {
        strengthInfantry.push_back(0.0f);
        strengthArcher.push_back(0.0f);
        strengthCavalry.push_back(0.0f);
        centroidX.push_back(0.0f);
        centroidY.push_back(0.0f);
        frontX.push_back(0.0f);
        frontY.push_back(0.0f);
        frontDirX.push_back(1.0f);
        frontDirY.push_back(0.0f);
        posture.push_back((uint8_t)ArmyPosture::Press);
        count++;
    }
};

// Combat weight of a squad: live members times their type's weight.
float squadStrength(const SquadHot& squads, size_t squadIndex);

// Recomputes both armies' aggregates and front lines from the squad tier.
// Serial, and called once per tick before any role assignment.
//
// Walks squads in ascending index order and accumulates on one thread, so the
// sums are bit-reproducible regardless of worker count.
void updateArmyAggregate(const SquadHot& squads, ArmyHot& armies);

// Assigns a role, a ward, and a target to every live squad of `team`
// (design 7.4). Writes only that team's squads.
//
// Serial, and deterministic by construction: every loop walks squads in
// ascending index and every tie breaks on the lower index. That is what makes
// the result identical on every platform and at every worker count, which
// matters because role and targetSquad both reach the state digest.
//
// Cost is O(ownSquads x enemySquads), about 40,000 operations at 200 squads a
// side, run once per team per kArmyDecideInterval ticks. Negligible against a
// 10,000-agent tick, and the reason an O(squads squared) assignment is
// affordable where an O(soldiers squared) one would not be.
void assignRoles(SquadHot& squads, const ArmyHot& armies, Team team);
