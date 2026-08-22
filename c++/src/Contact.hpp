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
// `scratch` is a caller-owned neighbour buffer, reused across squads so this
// does not heap-allocate per squad per tick.
void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t squadIndex, float dt,
                   std::vector<uint32_t>& scratch);
