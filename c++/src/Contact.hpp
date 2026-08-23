#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;   // defined in Simulation.hpp
struct SquadHot;     // defined in Squads.hpp
class SpatialHash;

// Sets squad `s`'s contact flag from whether its FRONT RANK has enemies in
// reach (design 5.1).
//
// Parallel-safe inside phaseSquadAggregate, but only because of what it reads:
// soldier positions (finalized last tick, read-only for this whole tick) and
// the spatial hash. It MUST NOT read any other squad's centroid, because every
// squad's centroid is being written by its own concurrent job in that same
// phase. This is the identical hazard updateSquadAggregate documents for
// facing, and it is why contact is measured against soldiers rather than
// against squads.
//
// Walks the grid in place rather than collecting candidates into a buffer, so
// this costs no scratch storage and no copy per front-rank soldier.
void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t squadIndex, float dt);

// Displaces soldier `i` out of any overlap with its neighbours (design 5.3).
//
// Reads positions from the caller's `nextX`/`nextY` snapshot, NOT from
// soldiers.posX/posY, and writes only soldiers.posX[i]/posY[i]. That split is
// what makes this parallel-safe: every agent's displacement is a function of
// read-only shared data and its own index, so the result is identical at any
// worker count and any chunking.
//
// Applies HALF of each overlap. The neighbour's own call applies the other
// half, so a pair separates symmetrically without either side writing the
// other. One pass per tick, deliberately not solved to convergence: an
// instantly-resolved constraint reads as a rigid body, a gradually-resolved
// one reads as a press of bodies.
void resolveOverlap(SoldierHot& soldiers,
                    const std::vector<float>& nextX,
                    const std::vector<float>& nextY,
                    const SpatialHash& hash, size_t soldierIndex);
