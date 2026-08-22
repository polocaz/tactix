#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;
struct SquadHot;

// World position of a formation slot, rotating the squad-local offset by the
// squad's facing. Facing's unit-length invariant is established at
// deployment (Simulation::init, before this is ever called) and maintained
// every tick after by updateSquadAggregate.
Vec2 slotWorldPosition(const SquadHot& squads, size_t squadIndex,
                       uint16_t slotIndex, uint32_t memberCount);

// Steers one soldier toward its slot. Writes only that soldier, so it is
// safe to call from a parallel phase.
void steerToSlot(SoldierHot& soldiers, const SquadHot& squads,
                 size_t soldierIndex, float dt);

// Same arrival behaviour, against a target the caller has already resolved.
// The live simulation uses this rather than steerToSlot: only Simulation
// knows where the obstacles are, so only Simulation can slide a slot that
// landed inside a wall out to a point a soldier can actually stand on
// (Simulation::clearOfObstacles).
void steerToward(SoldierHot& soldiers, size_t soldierIndex, Vec2 target, float dt);
