#include "platform.h"
#include "Simulation.hpp"
#include "StateDigest.hpp"
#include "DetMath.hpp"
#include <cassert>
#include <cmath>
#include <chrono>
#include "spdlog/spdlog.h"

Simulation::Simulation(int w, int h, uint32_t seed, uint32_t workerThreads)
    : screenWidth(w), screenHeight(h)
    , worldSeed(seed)
    , spatialHash(static_cast<float>(w), static_cast<float>(h), 50.0f)  // 50 pixel cells (Design Doc §5.1)
    , jobSystem(workerThreads)
{
    spatialHash.setCounters(&workCounters);
}

void Simulation::init(size_t count) {
    spdlog::info("Initializing {} agents", count);
    // init() runs before any tick, so tick 0 is reserved for setup draws.
    const Rng rng{worldSeed, 0u};
    entities.reserve(count);
    prevPosX.reserve(count);
    prevPosY.reserve(count);

    for (size_t i = 0; i < count; i++) {
        const uint32_t agent = (uint32_t)entities.count;
        const float px = (float)rng.range(agent, RngUse::SpawnPosX, 0, screenWidth);
        const float py = (float)rng.range(agent, RngUse::SpawnPosY, 0, screenHeight);
        const float vx = (float)rng.range(agent, RngUse::SpawnVelX, -10, 10);
        const float vy = (float)rng.range(agent, RngUse::SpawnVelY, -10, 10);
        entities.spawn(px, py, vx, vy);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }

    generateObstacles();
}

void Simulation::reset(size_t count) {
    entities.posX.clear();
    entities.posY.clear();
    entities.velX.clear();
    entities.velY.clear();
    entities.dirX.clear();
    entities.dirY.clear();
    entities.count = 0;

    prevPosX.clear();
    prevPosY.clear();

    buildings.clear();
    trees.clear();

    tickNumber = 0;

    init(count);
}

void Simulation::generateObstacles() {
    // Runs before any tick; no agent involved, so the obstacle index keys the draw.
    const Rng rng{worldSeed, 0u};

    // City blocks (buildings)
    const int blockCount = 8;
    for (int i = 0; i < blockCount; i++) {
        float x = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingX, 100, screenWidth - 200);
        float y = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingY, 100, screenHeight - 200);
        float w = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingW, 80, 150);
        float h = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingH, 80, 150);
        buildings.push_back({x, y, w, h});
    }

    // Scattered trees
    const int treeCount = 30;
    for (int i = 0; i < treeCount; i++) {
        float x = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeX, 50, screenWidth - 50);
        float y = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeY, 50, screenHeight - 50);
        float r = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeRadius, 15, 25);
        trees.push_back({x, y, r});
    }

    spdlog::info("Generated {} buildings and {} trees", buildings.size(), trees.size());
}

void Simulation::tick(float dt) {
    if (paused) return;

    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};

    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }

    rebuildSpatialHash();
    jobSystem.resetJobCounter();

    updateSeparation(dt, rng);
    updateMovement(dt);
    screenWrap();
}

void Simulation::rebuildSpatialHash() {
    auto start = std::chrono::steady_clock::now();

    spatialHash.clear();
    for (size_t i = 0; i < entities.count; i++) {
        spatialHash.insert(static_cast<uint32_t>(i), entities.posX[i], entities.posY[i]);
    }

    auto end = std::chrono::steady_clock::now();
    lastSpatialHashTime = std::chrono::duration<float>(end - start).count() * 1000.0f;  // ms
}

void Simulation::updateSeparation(float dt, const Rng& rng) {
    // Parallelize collision avoidance (Design Doc §6.2)
    const size_t chunkSize = 256;  // Job granularity

    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        // rng captured BY VALUE (8 bytes): the job outlives this stack frame.
        jobSystem.submit([this, start, end, dt, rng]() {
            updateSeparationChunk(start, end, dt, rng);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }

    jobSystem.waitAll();  // Barrier (Design Doc §6.3)
}

void Simulation::updateSeparationChunk(size_t start, size_t end, float dt, Rng rng) {
    // Collision avoidance using spatial queries (Phase 2)
    const float separationRadius = 25.0f;  // Increased from 20
    const float separationStrength = 300.0f;  // Increased from 200
    const float separationRadiusSq = separationRadius * separationRadius;

    // Thread-local neighbor buffer
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);

    for (size_t i = start; i < end; i++) {
        float px = entities.posX[i];
        float py = entities.posY[i];

        // Query nearby neighbors (Design Doc §5.4)
        spatialHash.queryNeighbors(px, py, separationRadius, localNeighbors);

        float steerX = 0.0f;
        float steerY = 0.0f;

        // Calculate separation force from neighbors
        for (uint32_t neighborIdx : localNeighbors) {
            if (neighborIdx == i) continue;  // Skip self

            float dx = px - entities.posX[neighborIdx];
            float dy = py - entities.posY[neighborIdx];
            float distSq = dx * dx + dy * dy;

            if (distSq < separationRadiusSq && distSq > 0.01f) {
                float dist = std::sqrt(distSq);
                // Stronger force when closer
                float force = (separationRadius - dist) / separationRadius;
                steerX += (dx / dist) * force;
                steerY += (dy / dist) * force;
            }
        }

        // Obstacle avoidance - buildings (rectangles)
        for (size_t b = 0; b < buildings.size(); b++) {
            const auto& building = buildings[b];
            // Find closest point on rectangle to agent
            float closestX = std::max(building.x, std::min(px, building.x + building.width));
            float closestY = std::max(building.y, std::min(py, building.y + building.height));

            float dx = px - closestX;
            float dy = py - closestY;
            float distSq = dx * dx + dy * dy;

            const float obstacleAvoidDist = 50.0f;  // Start avoiding earlier
            if (distSq < obstacleAvoidDist * obstacleAvoidDist) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly in any direction.
                    // Buildings are generated without overlap rejection, so one agent
                    // can be inside two at once and reach this line twice per tick.
                    // The obstacle index is folded into the key: without it both draws
                    // share an input, always return the same sign, and can only
                    // reinforce -- the old code's cancelling case became unreachable.
                    const uint32_t key = (uint32_t)(i * buildings.size() + b);
                    steerX += (rng.range(key, RngUse::SeparationPushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationPushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (obstacleAvoidDist - dist) / obstacleAvoidDist;
                    steerX += (dx / dist) * force * 5.0f;  // Much stronger avoidance
                    steerY += (dy / dist) * force * 5.0f;
                }
            }
        }

        // Obstacle avoidance - trees (circles)
        for (size_t t = 0; t < trees.size(); t++) {
            const auto& tree = trees[t];
            float dx = px - tree.x;
            float dy = py - tree.y;
            float distSq = dx * dx + dy * dy;
            float avoidRadius = tree.radius + 20.0f;  // Extra buffer

            if (distSq < avoidRadius * avoidRadius) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly. Same per-obstacle keying as
                    // the building push above; two tree centres within 0.1px of each
                    // other is practically unreachable, but the shape should match.
                    const uint32_t key = (uint32_t)(i * trees.size() + t);
                    steerX += (rng.range(key, RngUse::SeparationTreePushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationTreePushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (avoidRadius - dist) / avoidRadius;
                    steerX += (dx / dist) * force * 5.0f;
                    steerY += (dy / dist) * force * 5.0f;
                }
            }
        }

        // Apply separation steering
        entities.velX[i] += steerX * separationStrength * dt;
        entities.velY[i] += steerY * separationStrength * dt;

        // Limit velocity
        const float maxSpeed = 150.0f;
        float speedSq = entities.velX[i] * entities.velX[i] + entities.velY[i] * entities.velY[i];
        if (speedSq > maxSpeed * maxSpeed) {
            float speed = std::sqrt(speedSq);
            entities.velX[i] = (entities.velX[i] / speed) * maxSpeed;
            entities.velY[i] = (entities.velY[i] / speed) * maxSpeed;
        }
    }
}

void Simulation::updateMovement(float dt) {
    // Parallelize movement integration (Design Doc §6.2)
    const size_t chunkSize = 256;

    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        jobSystem.submit([this, start, end, dt]() {
            updateMovementChunk(start, end, dt);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }

    jobSystem.waitAll();  // Barrier
}

void Simulation::updateMovementChunk(size_t start, size_t end, float dt) {
    // SIMD-friendly: compiler auto-vectorizes this loop
    for (size_t i = start; i < end; i++) {
        float newX = entities.posX[i] + entities.velX[i] * dt;
        float newY = entities.posY[i] + entities.velY[i] * dt;

        // Check collision with buildings
        bool blocked = false;
        for (const auto& building : buildings) {
            if (newX > building.x - 5 && newX < building.x + building.width + 5 &&
                newY > building.y - 5 && newY < building.y + building.height + 5) {
                // Inside or very close to building - block movement
                blocked = true;
                // Find which side we hit
                float centerX = building.x + building.width / 2.0f;
                float centerY = building.y + building.height / 2.0f;
                float dx = entities.posX[i] - centerX;
                float dy = entities.posY[i] - centerY;

                // Push out and deflect velocity
                if (std::abs(dx) > std::abs(dy)) {
                    // Hit horizontal side - deflect horizontally, keep Y velocity
                    newX = entities.posX[i] + (dx > 0 ? 2.0f : -2.0f);
                    entities.velX[i] = -entities.velX[i] * 0.3f;  // Bounce back weakly
                    // Keep Y velocity to slide along wall
                } else {
                    // Hit vertical side - deflect vertically, keep X velocity
                    newY = entities.posY[i] + (dy > 0 ? 2.0f : -2.0f);
                    entities.velY[i] = -entities.velY[i] * 0.3f;  // Bounce back weakly
                    // Keep X velocity to slide along wall
                }
                break;
            }
        }

        // Check collision with trees
        if (!blocked) {
            for (const auto& tree : trees) {
                float dx = newX - tree.x;
                float dy = newY - tree.y;
                float distSq = dx * dx + dy * dy;
                if (distSq < tree.radius * tree.radius) {
                    // Inside tree - block and push out
                    blocked = true;
                    float dist = std::sqrt(distSq + 0.01f);
                    newX = tree.x + (dx / dist) * (tree.radius + 2.0f);
                    newY = tree.y + (dy / dist) * (tree.radius + 2.0f);
                    // Deflect velocity tangentially (slide around)
                    float normalX = dx / dist;
                    float normalY = dy / dist;
                    float velDotNormal = entities.velX[i] * normalX + entities.velY[i] * normalY;
                    entities.velX[i] -= normalX * velDotNormal * 1.5f;  // Remove normal component
                    entities.velY[i] -= normalY * velDotNormal * 1.5f;
                    break;
                }
            }
        }

        entities.posX[i] = newX;
        entities.posY[i] = newY;

        // Update direction from velocity (for rendering)
        float speed = std::sqrt(entities.velX[i] * entities.velX[i] +
                               entities.velY[i] * entities.velY[i]);
        if (speed > 0.1f) {  // Only update if moving
            entities.dirX[i] = entities.velX[i] / speed;
            entities.dirY[i] = entities.velY[i] / speed;
        }
    }
}

void Simulation::screenWrap() {
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    const float damping = 0.5f; // Bounce damping factor

    for (size_t i = 0; i < entities.count; i++) {
        // Left boundary
        if (entities.posX[i] < 0) {
            entities.posX[i] = 0;
            entities.velX[i] = std::abs(entities.velX[i]) * damping; // Bounce right
        }
        // Right boundary
        if (entities.posX[i] > w) {
            entities.posX[i] = w;
            entities.velX[i] = -std::abs(entities.velX[i]) * damping; // Bounce left
        }
        // Top boundary
        if (entities.posY[i] < 0) {
            entities.posY[i] = 0;
            entities.velY[i] = std::abs(entities.velY[i]) * damping; // Bounce down
        }
        // Bottom boundary
        if (entities.posY[i] > h) {
            entities.posY[i] = h;
            entities.velY[i] = -std::abs(entities.velY[i]) * damping; // Bounce up
        }
    }
}

uint32_t Simulation::getMaxCellOccupancy() const {
    return spatialHash.getMaxOccupancy();
}

uint64_t Simulation::stateDigest() const {
    StateDigest d;
    d.mix(static_cast<uint32_t>(entities.count));
    for (size_t i = 0; i < entities.count; ++i) {
        d.mix(entities.posX[i]);
        d.mix(entities.posY[i]);
        d.mix(entities.velX[i]);
        d.mix(entities.velY[i]);
    }
    return d.value();
}
