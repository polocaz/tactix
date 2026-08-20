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
