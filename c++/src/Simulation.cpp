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
    spdlog::info("Initializing {} agents with zombie simulation", count);
    // init() runs before any tick, so tick 0 is reserved for setup draws.
    const Rng rng{worldSeed, 0u};
    entities.reserve(count);
    prevPosX.reserve(count);
    prevPosY.reserve(count);

    // Population distribution: 90% civilians, 5% zombies, 5% heroes
    size_t civilianCount = static_cast<size_t>(count * 0.90f);
    size_t zombieCount = static_cast<size_t>(count * 0.05f);
    size_t heroCount = count - civilianCount - zombieCount;

    // Spawn civilians near buildings (residential areas)
    for (size_t i = 0; i < civilianCount; i++) {
        float px, py;
        if (i < buildings.size() && !buildings.empty()) {
            // Spawn near a building
            const auto& building = buildings[i % buildings.size()];
            // Key on the agent's GLOBAL index, not the per-loop counter: each of
            // the three spawn loops restarts i at 0, which would correlate the
            // draws of civilian #0, zombie #0 and hero #0.
            const uint32_t agent = (uint32_t)entities.count;
            px = building.x + building.width / 2.0f
               + (float)rng.range(agent, RngUse::SpawnNearBuildingOffsetX, -60, 60);
            py = building.y + building.height / 2.0f
               + (float)rng.range(agent, RngUse::SpawnNearBuildingOffsetY, -60, 60);
        } else {
            px = (float)rng.range((uint32_t)entities.count, RngUse::SpawnPosX, 0, screenWidth);
            py = (float)rng.range((uint32_t)entities.count, RngUse::SpawnPosY, 0, screenHeight);
        }
        float vx = (float)rng.range((uint32_t)entities.count, RngUse::SpawnVelX, -10, 10);
        float vy = (float)rng.range((uint32_t)entities.count, RngUse::SpawnVelY, -10, 10);
        entities.spawn(px, py, vx, vy, AgentType::Civilian, rng);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn zombies at graveyard (bottom-left area)
    for (size_t i = 0; i < zombieCount; i++) {
        const uint32_t agent = (uint32_t)entities.count;
        float px = (float)rng.range(agent, RngUse::SpawnZombiePosX, 50, 250);  // Graveyard zone
        float py = (float)rng.range(agent, RngUse::SpawnZombiePosY, screenHeight - 250, screenHeight - 50);
        float vx = (float)rng.range(agent, RngUse::SpawnZombieVelX, -8, 8);
        float vy = (float)rng.range(agent, RngUse::SpawnZombieVelY, -8, 8);
        entities.spawn(px, py, vx, vy, AgentType::Zombie, rng);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn heroes spread out (strategic positions)
    for (size_t i = 0; i < heroCount; i++) {
        // Spread heroes around perimeter
        const uint32_t agent = (uint32_t)entities.count;
        float px = (float)rng.range(agent, RngUse::SpawnHeroPosX, screenWidth / 3, screenWidth * 2 / 3);
        float py = (float)rng.range(agent, RngUse::SpawnHeroPosY, 50, 200);  // Top area
        float vx = (float)rng.range(agent, RngUse::SpawnHeroVelX, -12, 12);
        float vy = (float)rng.range(agent, RngUse::SpawnHeroVelY, -12, 12);
        entities.spawn(px, py, vx, vy, AgentType::Hero, rng);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Calculate memory usage
    size_t memoryPerEntity = sizeof(float) * 6 + sizeof(AgentType) + sizeof(uint8_t);  
    float totalMB = (memoryPerEntity * count) / (1024.0f * 1024.0f);
    spdlog::info("Memory usage: {:.2f} MB ({} bytes/entity)", totalMB, memoryPerEntity);
    spdlog::info("Spatial grid: {} cells", spatialHash.getCellCount());
    spdlog::info("Population - Civilians: {}, Zombies: {}, Heroes: {}", 
                 civilianCount, zombieCount, heroCount);
    
    // Generate environment obstacles
    generateObstacles();
    
    // Set graveyard bounds
    graveyard.x = 50;
    graveyard.y = screenHeight - 250;
    graveyard.width = 200;
    graveyard.height = 200;
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

void Simulation::setAgentCount(size_t count) {
    if (count == entities.count) return;

    // Like init(), this runs outside the tick loop.
    const Rng rng{worldSeed, 0u};
    
    if (count > entities.count) {
        // Add more agents with proper distribution
        size_t toAdd = count - entities.count;
        size_t civiliansToAdd = static_cast<size_t>(toAdd * 0.90f);
        size_t zombiesToAdd = static_cast<size_t>(toAdd * 0.05f);
        size_t heroesToAdd = toAdd - civiliansToAdd - zombiesToAdd;
        
        for (size_t i = 0; i < civiliansToAdd; i++) {
            const uint32_t agent = (uint32_t)entities.count;
            float px = (float)rng.range(agent, RngUse::AddCivilianPosX, 0, screenWidth);
            float py = (float)rng.range(agent, RngUse::AddCivilianPosY, 0, screenHeight);
            float vx = (float)rng.range(agent, RngUse::AddCivilianVelX, -20, 20);
            float vy = (float)rng.range(agent, RngUse::AddCivilianVelY, -20, 20);
            entities.spawn(px, py, vx, vy, AgentType::Civilian, rng);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        
        for (size_t i = 0; i < zombiesToAdd; i++) {
            const uint32_t agent = (uint32_t)entities.count;
            float px = (float)rng.range(agent, RngUse::AddZombiePosX, 0, screenWidth);
            float py = (float)rng.range(agent, RngUse::AddZombiePosY, 0, screenHeight);
            float vx = (float)rng.range(agent, RngUse::AddZombieVelX, -15, 15);
            float vy = (float)rng.range(agent, RngUse::AddZombieVelY, -15, 15);
            entities.spawn(px, py, vx, vy, AgentType::Zombie, rng);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        
        for (size_t i = 0; i < heroesToAdd; i++) {
            const uint32_t agent = (uint32_t)entities.count;
            float px = (float)rng.range(agent, RngUse::AddHeroPosX, 0, screenWidth);
            float py = (float)rng.range(agent, RngUse::AddHeroPosY, 0, screenHeight);
            float vx = (float)rng.range(agent, RngUse::AddHeroVelX, -25, 25);
            float vy = (float)rng.range(agent, RngUse::AddHeroVelY, -25, 25);
            entities.spawn(px, py, vx, vy, AgentType::Hero, rng);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        spdlog::info("Added {} agents (total: {})", toAdd, entities.count);
    } else {
        // Remove agents
        size_t toRemove = entities.count - count;
        entities.count = count;
        entities.posX.resize(count);
        entities.posY.resize(count);
        entities.velX.resize(count);
        entities.velY.resize(count);
        entities.dirX.resize(count);
        entities.dirY.resize(count);
        entities.type.resize(count);
        entities.health.resize(count);
        prevPosX.resize(count);
        prevPosY.resize(count);
        spdlog::info("Removed {} agents (total: {})", toRemove, entities.count);
    }
}

void Simulation::tick(float dt) {
    if (paused) return;  // Skip tick if paused

    // One Rng per tick. Stateless, so every worker thread can draw from it
    // concurrently and in any order and still get the same answer.
    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};
    
    // Store previous positions for interpolation
    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }
    
    // Update gunshot lifetimes and remove expired ones
    for (auto it = recentGunshots.begin(); it != recentGunshots.end();) {
        it->lifetime -= dt;
        if (it->lifetime <= 0.0f) {
            it = recentGunshots.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update gunshot line visuals (fade quickly)
    for (auto it = gunshotLines.begin(); it != gunshotLines.end();) {
        it->lifetime -= dt;
        if (it->lifetime <= 0.0f) {
            it = gunshotLines.erase(it);
        } else {
            ++it;
        }
    }
    
    // Process ranged kills from heroes (collect from behavior chunk)
    std::vector<size_t> zombiesToKill;
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] == AgentType::Hero && 
            entities.state[i] == AgentState::Pursuing &&
            entities.shootCooldown[i] > 1.45f) {  // Just shot (cooldown near max)
            
            // Decode shooter and target from lastSeen hack
            size_t shooterIdx = (size_t)entities.lastSeenX[i];
            size_t targetIdx = (size_t)entities.lastSeenY[i];
            
            if (shooterIdx < entities.count && targetIdx < entities.count &&
                entities.type[targetIdx] == AgentType::Zombie) {
                
                float heroX = entities.posX[shooterIdx];
                float heroY = entities.posY[shooterIdx];
                float zombieX = entities.posX[targetIdx];
                float zombieY = entities.posY[targetIdx];
                
                // Create gunshot sound marker
                recentGunshots.push_back({heroX, heroY, 3.0f});
                
                // Create visual line
                gunshotLines.push_back({heroX, heroY, zombieX, zombieY, 0.15f});
                
                // Damage the zombie (takes 3 hits to kill)
                if (entities.health[targetIdx] > 0) {
                    entities.health[targetIdx]--;
                    if (entities.health[targetIdx] == 0) {
                        // Zombie dies after 3 hits
                        zombiesToKill.push_back(targetIdx);
                    }
                }
                
                // Decrement hero health (tracks kills)
                if (entities.health[shooterIdx] > 0) {
                    entities.health[shooterIdx]--;
                    if (entities.health[shooterIdx] == 0) {
                        entities.type[shooterIdx] = AgentType::Zombie;
                        entities.health[shooterIdx] = 3;  // New zombie has 3 health
                        spdlog::info("Hero {} exhausted after 5 kills, turned zombie!", shooterIdx);
                    }
                }
            }
        }
    }
    
    // Remove killed zombies
    std::sort(zombiesToKill.begin(), zombiesToKill.end(), std::greater<size_t>());
    for (size_t idx : zombiesToKill) {
        // Remove by swapping with last and popping
        if (idx < entities.count - 1) {
            size_t last = entities.count - 1;
            entities.posX[idx] = entities.posX[last];
            entities.posY[idx] = entities.posY[last];
            entities.velX[idx] = entities.velX[last];
            entities.velY[idx] = entities.velY[last];
            entities.dirX[idx] = entities.dirX[last];
            entities.dirY[idx] = entities.dirY[last];
            entities.type[idx] = entities.type[last];
            entities.state[idx] = entities.state[last];
            entities.health[idx] = entities.health[last];
            entities.lastSeenX[idx] = entities.lastSeenX[last];
            entities.lastSeenY[idx] = entities.lastSeenY[last];
            entities.searchTimer[idx] = entities.searchTimer[last];
            entities.patrolTargetX[idx] = entities.patrolTargetX[last];
            entities.patrolTargetY[idx] = entities.patrolTargetY[last];
            entities.shootCooldown[idx] = entities.shootCooldown[last];
            entities.aimTimer[idx] = entities.aimTimer[last];
            entities.fleeStrategy[idx] = entities.fleeStrategy[last];
            entities.heroType[idx] = entities.heroType[last];
            prevPosX[idx] = prevPosX[last];
            prevPosY[idx] = prevPosY[last];
        }
        entities.posX.pop_back();
        entities.posY.pop_back();
        entities.velX.pop_back();
        entities.velY.pop_back();
        entities.dirX.pop_back();
        entities.dirY.pop_back();
        entities.type.pop_back();
        entities.state.pop_back();
        entities.health.pop_back();
        entities.lastSeenX.pop_back();
        entities.lastSeenY.pop_back();
        entities.searchTimer.pop_back();
        entities.patrolTargetX.pop_back();
        entities.patrolTargetY.pop_back();
        entities.shootCooldown.pop_back();
        entities.aimTimer.pop_back();
        entities.fleeStrategy.pop_back();
        entities.heroType.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
        entities.count--;
    }
    
    // Rebuild spatial hash (Design Doc §5.3)
    rebuildSpatialHash();
    
    // Reset job counter for metrics
    jobSystem.resetJobCounter();
    
    // Update behaviors in parallel (Design Doc §6.2)
    updateSeparation(dt, rng);  // Collision avoidance using spatial queries
    updateBehaviors(dt, rng);   // Seek/flee/combat behaviors for zombie simulation
    updateMovement(dt);         // Apply velocities
    
    // Process infections (main thread, requires state changes)
    updateInfections(rng);
    
    // Screen wrapping
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

size_t Simulation::getCivilianCount() const {
    size_t count = 0;
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] == AgentType::Civilian) count++;
    }
    return count;
}

size_t Simulation::getZombieCount() const {
    size_t count = 0;
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] == AgentType::Zombie) count++;
    }
    return count;
}

size_t Simulation::getHeroCount() const {
    size_t count = 0;
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] == AgentType::Hero) count++;
    }
    return count;
}

uint64_t Simulation::stateDigest() const {
    StateDigest d;
    d.mix(static_cast<uint32_t>(entities.count));
    for (size_t i = 0; i < entities.count; ++i) {
        d.mix(entities.posX[i]);
        d.mix(entities.posY[i]);
        d.mix(entities.velX[i]);
        d.mix(entities.velY[i]);
        d.mix(static_cast<uint32_t>(entities.type[i]));
        d.mix(static_cast<uint32_t>(entities.state[i]));
        d.mix(static_cast<uint32_t>(entities.health[i]));
    }
    return d.value();
}

void Simulation::updateBehaviors(float dt, const Rng& rng) {
    // Parallelize behavior updates
    const size_t chunkSize = 256;
    
    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        // rng captured BY VALUE (8 bytes): the job outlives this stack frame.
        jobSystem.submit([this, start, end, dt, rng]() {
            updateBehaviorsChunk(start, end, dt, rng);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
    
    jobSystem.waitAll();
}

void Simulation::updateBehaviorsChunk(size_t start, size_t end, float dt, Rng rng) {
    const float seekRadius = 150.0f;  // Detection range
    const float searchDuration = 3.0f;  // Seconds to search last known location
    const float wanderStrength = 20.0f;
    
    // Thread-local neighbor buffer
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);
    
    for (size_t i = start; i < end; i++) {
        AgentType myType = entities.type[i];
        AgentState myState = entities.state[i];
        
        // Skip dead agents (corpses don't move)
        if (myState == AgentState::Dead) {
            entities.velX[i] = 0.0f;
            entities.velY[i] = 0.0f;
            continue;
        }
        
        // Fighting agents use special combat movement
        if (myState == AgentState::Fighting) {
            uint32_t targetIdx = entities.combatTarget[i];
            if (targetIdx != UINT32_MAX && targetIdx < entities.count) {
                // Face the opponent (lock direction)
                float dx = entities.posX[targetIdx] - entities.posX[i];
                float dy = entities.posY[targetIdx] - entities.posY[i];
                float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                entities.dirX[i] = dx / dist;
                entities.dirY[i] = dy / dist;
                
                // Smooth struggle animation using elapsed time
                // Use a combination of frequencies for organic feel
                // Derived from the tick counter, not accumulated in a function-local
                // static: that static was incremented from every worker thread
                // without synchronisation, losing updates non-deterministically.
                const float elapsedTime = (float)rng.tick * (1.0f / 60.0f);
                float phase = static_cast<float>(i) * 0.7f;  // Each agent has different phase
                
                // Perpendicular to facing direction for side-to-side shake
                float perpX = -entities.dirY[i];
                float perpY = entities.dirX[i];
                
                // Smooth sine wave shake (no random jitter)
                float shake = detmath::sin(elapsedTime * 12.0f + phase) * 1.5f;
                
                // Subtle push/pull toward opponent
                float pushPull = detmath::sin(elapsedTime * 4.0f + phase) * 0.5f;
                
                entities.velX[i] = perpX * shake + entities.dirX[i] * pushPull;
                entities.velY[i] = perpY * shake + entities.dirY[i] * pushPull;
            }
            continue;
        }
        
        // Bitten agents flee desperately with reduced speed
        if (myState == AgentState::Bitten) {
            // Speed reduces as infection progresses
            float healthySpeed = 40.0f;
            float sickSpeed = healthySpeed * (1.0f - entities.infectionProgress[i] * 0.5f);
            
            // Flee from any nearby zombies
            float fleeX = 0.0f, fleeY = 0.0f;
            int threatCount = 0;
            
            float px = entities.posX[i];
            float py = entities.posY[i];
            spatialHash.queryNeighbors(px, py, 100.0f, localNeighbors);
            
            for (uint32_t neighborIdx : localNeighbors) {
                if (entities.type[neighborIdx] == AgentType::Zombie) {
                    float dx = px - entities.posX[neighborIdx];
                    float dy = py - entities.posY[neighborIdx];
                    float distSq = dx * dx + dy * dy;
                    if (distSq > 0.01f) {
                        float dist = std::sqrt(distSq);
                        fleeX += (dx / dist);
                        fleeY += (dy / dist);
                        threatCount++;
                    }
                }
            }
            
            if (threatCount > 0) {
                float len = std::sqrt(fleeX * fleeX + fleeY * fleeY + 0.01f);
                entities.velX[i] = (fleeX / len) * sickSpeed;
                entities.velY[i] = (fleeY / len) * sickSpeed;
            } else {
                // Wander slowly
                entities.velX[i] *= 0.95f;
                entities.velY[i] *= 0.95f;
            }
            continue;
        }
        
        float px = entities.posX[i];
        float py = entities.posY[i];
        
        // Query nearby agents
        spatialHash.queryNeighbors(px, py, seekRadius, localNeighbors);
        
        float desiredDirX = 0.0f;
        float desiredDirY = 0.0f;
        int targetCount = 0;
        bool targetFound = false;
        
        // Target speeds for each agent type
        float targetSpeed = 40.0f;  // Base civilian speed (was 60)
        if (myType == AgentType::Zombie) targetSpeed = 35.0f;    // Zombies slightly slower (was 55)
        else if (myType == AgentType::Hero) targetSpeed = 50.0f;  // Heroes fastest (was 75)
        
        // Different behaviors based on agent type and state
        if (myType == AgentType::Civilian) {
            // Check for nearby zombies and heroes
            float nearestHeroDist = 1e9f;
            float nearestHeroX = 0.0f, nearestHeroY = 0.0f;
            
            for (uint32_t neighborIdx : localNeighbors) {
                if (entities.type[neighborIdx] == AgentType::Zombie) {
                    float dx = px - entities.posX[neighborIdx];
                    float dy = py - entities.posY[neighborIdx];
                    float distSq = dx * dx + dy * dy;
                    
                    if (distSq > 0.01f) {
                        float dist = std::sqrt(distSq);
                        float force = 1.0f - (dist / seekRadius);
                        desiredDirX += (dx / dist) * force;
                        desiredDirY += (dy / dist) * force;
                        targetCount++;
                        targetFound = true;
                        
                        // Update memory
                        entities.lastSeenX[i] = entities.posX[neighborIdx];
                        entities.lastSeenY[i] = entities.posY[neighborIdx];
                    }
                } else if (entities.type[neighborIdx] == AgentType::Hero) {
                    // Track nearest hero for flee-to-protection behavior
                    float dx = entities.posX[neighborIdx] - px;
                    float dy = entities.posY[neighborIdx] - py;
                    float distSq = dx * dx + dy * dy;
                    if (distSq < nearestHeroDist) {
                        nearestHeroDist = distSq;
                        nearestHeroX = entities.posX[neighborIdx];
                        nearestHeroY = entities.posY[neighborIdx];
                    }
                }
            }
            
            if (targetFound) {
                // Choose flee strategy on first detection (sticky decision)
                if (myState != AgentState::Fleeing) {
                    entities.fleeStrategy[i] = (rng.range((uint32_t)i, RngUse::FleeStrategyChoice, 0, 100) < 30) ? 1 : 0;
                }
                
                bool seekProtection = (entities.fleeStrategy[i] == 1) && nearestHeroDist < 1e8f;
                
                if (seekProtection) {
                    // Flee toward nearest hero for protection
                    float dx = nearestHeroX - px;
                    float dy = nearestHeroY - py;
                    float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                    desiredDirX = dx / dist;
                    desiredDirY = dy / dist;
                }
                
                entities.state[i] = AgentState::Fleeing;
                targetSpeed = 45.0f;  // Panic boost (was 65)
            } else if (myState == AgentState::Fleeing) {
                entities.state[i] = AgentState::Searching;
                entities.searchTimer[i] = searchDuration;
            }
            
            if (myState == AgentState::Searching) {
                entities.searchTimer[i] -= dt;
                desiredDirX = px - entities.lastSeenX[i];
                desiredDirY = py - entities.lastSeenY[i];
                targetCount = 1;
                targetSpeed = 50.0f;
                
                if (entities.searchTimer[i] <= 0) {
                    entities.state[i] = AgentState::Idle;
                }
            }
            
        } else if (myType == AgentType::Zombie) {
            // Seek civilians and heroes
            float closestDistSq = seekRadius * seekRadius;
            float cohesionX = 0.0f, cohesionY = 0.0f;
            int zombieCount = 0;
            
            // Check for recent gunshots (attracts zombies!)
            const float gunshotAttractionRadius = 300.0f;
            for (const auto& gunshot : recentGunshots) {
                float dx = gunshot.x - px;
                float dy = gunshot.y - py;
                float distSq = dx * dx + dy * dy;
                if (distSq < gunshotAttractionRadius * gunshotAttractionRadius) {
                    float dist = std::sqrt(distSq + 0.01f);
                    float force = 0.5f * (1.0f - dist / gunshotAttractionRadius);
                    desiredDirX += (dx / dist) * force;
                    desiredDirY += (dy / dist) * force;
                    targetCount++;
                }
            }
            
            for (uint32_t neighborIdx : localNeighbors) {
                AgentType neighborType = entities.type[neighborIdx];
                // CONCURRENCY INVARIANT: neighborIdx may belong to a different worker's chunk
                // and be concurrently written by that chunk's updateBehaviorsChunk() call this
                // same tick (writes to entities.state[] happen at ~814,817,829,893,895,926,977,
                // 1029,1042 above/below). That is safe here ONLY because the sole use of
                // neighborState is the `== AgentState::Dead` check below: no chunk function ever
                // writes AgentState::Dead (see the asserts guarding each state write, and the
                // early "skip dead agents" continue near the top of updateBehaviorsChunk), and a
                // Dead agent is never rewritten to a live state. So "is this agent Dead" is
                // invariant across the entire parallel phase regardless of interleaving or
                // worker/thread count, which is what makes this read race-free without a lock
                // and preserves the seed -> bit-identical-state guarantee at any thread count.
                // Do NOT read any other field of a neighbor's AgentState-adjacent data here
                // without re-checking this reasoning.
                AgentState neighborState = entities.state[neighborIdx];

                // Skip dead agents - zombies prefer live prey
                if (neighborState == AgentState::Dead) continue;
                
                if (neighborType == AgentType::Civilian || neighborType == AgentType::Hero) {
                    float dx = entities.posX[neighborIdx] - px;
                    float dy = entities.posY[neighborIdx] - py;
                    float distSq = dx * dx + dy * dy;
                    
                    if (distSq > 0.01f && distSq < closestDistSq) {
                        float dist = std::sqrt(distSq);
                        float force = 1.0f - (dist / seekRadius);
                        desiredDirX += (dx / dist) * force;
                        desiredDirY += (dy / dist) * force;
                        targetCount++;
                        targetFound = true;
                        
                        // Update memory
                        entities.lastSeenX[i] = entities.posX[neighborIdx];
                        entities.lastSeenY[i] = entities.posY[neighborIdx];
                        closestDistSq = distSq;
                        
                        // Lunge when close
                        if (dist < 30.0f) {
                            targetSpeed = 45.0f;  // Sprint! (was 65)
                        }
                    }
                } else if (neighborType == AgentType::Zombie) {
                    // Horde behavior - track zombie positions for cohesion
                    cohesionX += entities.posX[neighborIdx];
                    cohesionY += entities.posY[neighborIdx];
                    zombieCount++;
                }
            }
            
            if (targetFound) {
                entities.state[i] = AgentState::Pursuing;
            } else if (myState == AgentState::Pursuing) {
                entities.state[i] = AgentState::Searching;
                entities.searchTimer[i] = searchDuration * 2.0f;
            }
            
            if (myState == AgentState::Searching || myState == AgentState::Patrol) {
                // Form hordes when not actively pursuing
                if (zombieCount > 0 && !targetFound) {
                    cohesionX /= zombieCount;
                    cohesionY /= zombieCount;
                    float dx = cohesionX - px;
                    float dy = cohesionY - py;
                    float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                    if (dist > 10.0f) {  // Don't cluster too tightly
                        desiredDirX += (dx / dist) * 0.3f;  // Weak cohesion
                        desiredDirY += (dy / dist) * 0.3f;
                        targetCount++;
                    }
                }
            }
            
            if (myState == AgentState::Searching) {
                entities.searchTimer[i] -= dt;
                float dx = entities.lastSeenX[i] - px;
                float dy = entities.lastSeenY[i] - py;
                float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                desiredDirX += dx / dist;
                desiredDirY += dy / dist;
                targetCount = targetCount > 0 ? targetCount : 1;
                targetSpeed = 45.0f;
                
                if (dist < 5.0f || entities.searchTimer[i] <= 0) {
                    entities.state[i] = AgentState::Patrol;
                }
            }
            
        } else if (myType == AgentType::Hero) {
            // Update shoot cooldown and aim timer
            if (entities.shootCooldown[i] > 0.0f) {
                entities.shootCooldown[i] -= dt;
            }
            if (entities.aimTimer[i] > 0.0f) {
                entities.aimTimer[i] -= dt;
            }
            
            // Seek zombies aggressively
            float squadX = 0.0f, squadY = 0.0f;
            int heroCount = 0;
            float closestZombieDist = 1e9f;
            uint32_t closestZombieIdx = UINT32_MAX;
            
            for (uint32_t neighborIdx : localNeighbors) {
                if (entities.type[neighborIdx] == AgentType::Zombie) {
                    float dx = entities.posX[neighborIdx] - px;
                    float dy = entities.posY[neighborIdx] - py;
                    float distSq = dx * dx + dy * dy;
                    
                    if (distSq > 0.01f) {
                        float dist = std::sqrt(distSq);
                        float force = 1.0f - (dist / seekRadius);
                        desiredDirX += (dx / dist) * force;
                        desiredDirY += (dy / dist) * force;
                        targetCount++;
                        targetFound = true;
                        
                        if (dist < closestZombieDist) {
                            closestZombieDist = dist;
                            closestZombieIdx = neighborIdx;
                        }
                        
                        // Update memory
                        entities.lastSeenX[i] = entities.posX[neighborIdx];
                        entities.lastSeenY[i] = entities.posY[neighborIdx];
                    }
                } else if (entities.type[neighborIdx] == AgentType::Hero) {
                    // Squad coordination - track hero positions
                    squadX += entities.posX[neighborIdx];
                    squadY += entities.posY[neighborIdx];
                    heroCount++;
                }
            }
            
            if (targetFound) {
                entities.state[i] = AgentState::Pursuing;
                
                bool isHunter = entities.heroType[i] == 1;
                
                if (isHunter) {
                    // Hunters: chase down zombies aggressively
                    targetSpeed = 55.0f;  // (was 80)
                } else {
                    // Defenders: maintain distance, kite backwards
                    if (closestZombieDist < 70.0f) {
                        // Too close - back away while shooting
                        desiredDirX = -desiredDirX;  // Reverse direction
                        desiredDirY = -desiredDirY;
                        targetSpeed = 45.0f;  // Back up speed (was 70)
                    } else {
                        // Good distance - hold position (slow movement)
                        targetSpeed = 15.0f;  // (was 20)
                    }
                }
                
                // Shoot when aim timer completes (check this FIRST before resetting timer)
                bool justShot = false;
                if (entities.aimTimer[i] <= 0.0f && entities.aimTimer[i] > -10.0f &&  // Timer just expired
                    entities.shootCooldown[i] <= 0.0f && 
                    closestZombieDist < 100.0f && closestZombieIdx != UINT32_MAX) {
                    entities.shootCooldown[i] = 1.5f;  // 1.5 second cooldown
                    entities.aimTimer[i] = -100.0f;  // Mark as shot (prevent retriggering)
                    // Store shoot info for main thread to process
                    entities.lastSeenX[i] = (float)i;  // Store shooter index
                    entities.lastSeenY[i] = (float)closestZombieIdx;  // Store target index
                    justShot = true;
                }
                
                // Start aiming if we have a target and no aim timer (but didn't just shoot)
                if (!justShot && closestZombieDist < 100.0f && entities.aimTimer[i] <= 0.0f && entities.shootCooldown[i] <= 0.0f) {
                    // Variable aim delay: 0.3-0.6 seconds
                    entities.aimTimer[i] = 0.3f + ((float)rng.range((uint32_t)i, RngUse::HeroAimDelay, 0, 300) / 1000.0f);
                }
                
                // Squad cohesion when pursuing (only for defenders)
                if (!isHunter && heroCount > 0) {
                    squadX /= heroCount;
                    squadY /= heroCount;
                    float dx = squadX - px;
                    float dy = squadY - py;
                    float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                    if (dist > 15.0f) {  // Stay somewhat close to squad
                        desiredDirX += (dx / dist) * 0.3f;  // Stronger coordination for defenders
                        desiredDirY += (dy / dist) * 0.3f;
                    }
                }
            } else if (myState == AgentState::Pursuing) {
                entities.state[i] = AgentState::Searching;
                entities.searchTimer[i] = searchDuration * 1.5f;
            }
            
            if (myState == AgentState::Searching) {
                entities.searchTimer[i] -= dt;
                desiredDirX = entities.lastSeenX[i] - px;
                desiredDirY = entities.lastSeenY[i] - py;
                float dist = std::sqrt(desiredDirX * desiredDirX + desiredDirY * desiredDirY + 0.01f);
                targetCount = 1;
                targetSpeed = 65.0f;
                
                if (dist < 5.0f || entities.searchTimer[i] <= 0) {
                    entities.state[i] = AgentState::Patrol;
                }
            }
        }
        
        // Patrol behavior - pick random destinations and walk toward them
        if (entities.state[i] == AgentState::Patrol) {
            float dx = entities.patrolTargetX[i] - px;
            float dy = entities.patrolTargetY[i] - py;
            float distSq = dx * dx + dy * dy;
            
            // Reached patrol point or need new one
            if (distSq < 25.0f || distSq > 1e8f) {
                entities.patrolTargetX[i] = (float)rng.range((uint32_t)i, RngUse::PatrolRetargetX, 50, 1850);
                entities.patrolTargetY[i] = (float)rng.range((uint32_t)i, RngUse::PatrolRetargetY, 50, 1030);
                dx = entities.patrolTargetX[i] - px;
                dy = entities.patrolTargetY[i] - py;
                distSq = dx * dx + dy * dy;
            }
            
            if (distSq > 0.1f) {
                float dist = std::sqrt(distSq);
                desiredDirX = dx / dist;
                desiredDirY = dy / dist;
                targetCount = 1;
                targetSpeed *= 0.4f;  // Slow wandering (24/22/30 for civilian/zombie/hero)
            }
        }
        
        // Apply steering - direct velocity setting for instant direction changes
        if (targetCount > 0) {
            // Normalize desired direction
            float dirLength = std::sqrt(desiredDirX * desiredDirX + desiredDirY * desiredDirY + 0.001f);
            desiredDirX /= dirLength;
            desiredDirY /= dirLength;
            
            // Directly set velocity (instant response)
            entities.velX[i] = desiredDirX * targetSpeed;
            entities.velY[i] = desiredDirY * targetSpeed;
            
        } else {
            // Idle or no target - gradually slow down
            entities.velX[i] *= 0.9f;
            entities.velY[i] *= 0.9f;
        }
        
        // Sharp wall avoidance - aggressive direction change near boundaries
        const float dangerZone = 100.0f;  // Critical distance from wall
        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);
        
        bool nearWall = false;
        float wallAvoidX = 0.0f;
        float wallAvoidY = 0.0f;
        
        // Check proximity to each wall and calculate emergency steering
        if (px < dangerZone) {
            nearWall = true;
            float urgency = 1.0f - (px / dangerZone);
            // Redirect velocity sharply away from wall
            wallAvoidX = urgency * 2.0f;  // Strong rightward push
            // Also redirect current velocity perpendicular to wall
            if (entities.velX[i] < 0) {
                entities.velX[i] *= (1.0f - urgency);  // Dampen approach velocity
            }
        }
        if (px > w - dangerZone) {
            nearWall = true;
            float urgency = 1.0f - ((w - px) / dangerZone);
            wallAvoidX = -urgency * 2.0f;  // Strong leftward push
            if (entities.velX[i] > 0) {
                entities.velX[i] *= (1.0f - urgency);
            }
        }
        if (py < dangerZone) {
            nearWall = true;
            float urgency = 1.0f - (py / dangerZone);
            wallAvoidY = urgency * 2.0f;  // Strong downward push
            if (entities.velY[i] < 0) {
                entities.velY[i] *= (1.0f - urgency);
            }
        }
        if (py > h - dangerZone) {
            nearWall = true;
            float urgency = 1.0f - ((h - py) / dangerZone);
            wallAvoidY = -urgency * 2.0f;  // Strong upward push
            if (entities.velY[i] > 0) {
                entities.velY[i] *= (1.0f - urgency);
            }
        }
        
        // Apply sharp turn when near walls by directly modifying velocity direction
        if (nearWall) {
            // Blend wall avoidance direction with current velocity
            float blendFactor = 0.7f;  // Strong influence
            entities.velX[i] = entities.velX[i] * (1.0f - blendFactor) + wallAvoidX * targetSpeed * blendFactor;
            entities.velY[i] = entities.velY[i] * (1.0f - blendFactor) + wallAvoidY * targetSpeed * blendFactor;
        }
        
        // Clamp to max speed
        float speedSq = entities.velX[i] * entities.velX[i] + entities.velY[i] * entities.velY[i];
        float maxSpeed = targetSpeed * 1.1f;
        if (speedSq > maxSpeed * maxSpeed) {
            float speed = std::sqrt(speedSq);
            entities.velX[i] = (entities.velX[i] / speed) * maxSpeed;
            entities.velY[i] = (entities.velY[i] / speed) * maxSpeed;
        }

        // Debug-only guard for the concurrency invariant documented at the
        // `entities.state[neighborIdx]` read above: this parallel chunk function must never
        // write AgentState::Dead, since other worker threads may be concurrently reading a
        // neighbor's state and rely on "is it Dead" being stable for the whole parallel phase.
        // (Only updateInfections(), which runs single-threaded after this phase's jobSystem
        // .waitAll(), is allowed to write Dead.) If this fires, the determinism guarantee is
        // broken and needs a real fix, not a silenced assert.
        assert(entities.state[i] != AgentState::Dead &&
               "updateBehaviorsChunk must never write AgentState::Dead (breaks the "
               "cross-thread invariant that neighbor Dead-state reads rely on)");
    }
}

void Simulation::updateInfections(const Rng& rng) {
    const float meleeRange = 8.0f;  // Close combat range (reduced for tighter engagement)
    const float meleeRangeSq = meleeRange * meleeRange;
    const float feedRange = 20.0f;  // Range to feed on corpses
    const float feedRangeSq = feedRange * feedRange;
    const float dt = 1.0f / 60.0f;
    
    std::vector<size_t> zombiesToKill;  // Track zombies to remove
    std::vector<size_t> entitiesToKill;  // Track entities to remove
    std::vector<size_t> corpsesToRemove;  // Track corpses that get eaten
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);
    
    // Update bitten civilians (infection progression)
    for (size_t i = 0; i < entities.count; i++) {
        // Update combat cooldown
        if (entities.combatCooldown[i] > 0.0f) {
            entities.combatCooldown[i] -= dt;
        }
        
        if (entities.state[i] == AgentState::Bitten) {
            entities.infectionTimer[i] -= dt;
            entities.infectionProgress[i] = 1.0f - std::max(0.0f, entities.infectionTimer[i] / 15.0f);
            
            if (entities.infectionTimer[i] <= 0.0f) {
                // Infection kills civilian - becomes corpse
                entities.state[i] = AgentState::Dead;
                entities.velX[i] = 0.0f;
                entities.velY[i] = 0.0f;
                entities.reanimationTimer[i] = 3.0f + (rng.range((uint32_t)i, RngUse::InfectionDeathReanimationDelay, 0, 50) / 10.0f);
                spdlog::info("Civilian {} died from infection! Will reanimate in {:.1f}s", i, entities.reanimationTimer[i]);
            }
        }
    }
    
    // Update reanimation timers for dead civilians
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.state[i] == AgentState::Dead && entities.type[i] == AgentType::Civilian) {
            entities.reanimationTimer[i] -= dt;
            
            if (entities.reanimationTimer[i] <= 0.0f) {
                // Reanimate as zombie!
                entities.type[i] = AgentType::Zombie;
                entities.state[i] = AgentState::Patrol;
                entities.health[i] = 3;
                entities.meleeAttackCooldown[i] = 0.0f;
                entities.velX[i] = (rng.range((uint32_t)i, RngUse::ReanimateVelX, -10, 10) / 10.0f) * 20.0f;
                entities.velY[i] = (rng.range((uint32_t)i, RngUse::ReanimateVelY, -10, 10) / 10.0f) * 20.0f;
                spdlog::info("Corpse {} reanimated as zombie!", i);
            }
        }
    }
    
    // Update ongoing combat
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.state[i] == AgentState::Fighting) {
            entities.combatTimer[i] -= dt;
            
            if (entities.combatTimer[i] <= 0.0f) {
                // Combat resolves!
                uint32_t targetIdx = entities.combatTarget[i];
                if (targetIdx >= entities.count) {
                    // Target gone, exit combat
                    entities.state[i] = AgentState::Patrol;
                    entities.combatTarget[i] = UINT32_MAX;
                    continue;
                }
                
                AgentType myType = entities.type[i];
                AgentType targetType = entities.type[targetIdx];
                
                // Count nearby allies and enemies for bonuses
                float px = entities.posX[i];
                float py = entities.posY[i];
                spatialHash.queryNeighbors(px, py, 50.0f, localNeighbors);
                
                int nearbyAllies = 0;
                int nearbyEnemies = 0;
                for (uint32_t idx : localNeighbors) {
                    if (idx == i || idx == targetIdx) continue;
                    if (entities.type[idx] == myType) nearbyAllies++;
                    else if (entities.type[idx] == targetType) nearbyEnemies++;
                }
                
                // Resolve combat based on types
                if (myType == AgentType::Zombie && targetType == AgentType::Civilian) {
                    resolveCivilianVsZombieCombat(i, targetIdx, nearbyAllies, nearbyEnemies, zombiesToKill, entitiesToKill, rng);
                } else if (myType == AgentType::Civilian && targetType == AgentType::Zombie) {
                    resolveCivilianVsZombieCombat(targetIdx, i, nearbyEnemies, nearbyAllies, zombiesToKill, entitiesToKill, rng);
                } else if (myType == AgentType::Hero || targetType == AgentType::Hero) {
                    resolveHeroVsZombieCombat(i, targetIdx, zombiesToKill, entitiesToKill, rng);
                }
                
                // Exit combat state
                entities.state[i] = AgentState::Patrol;
                entities.combatTarget[i] = UINT32_MAX;
                entities.combatCooldown[i] = 2.0f;  // 2 second cooldown
                
                if (targetIdx < entities.count && entities.state[targetIdx] == AgentState::Fighting) {
                    entities.state[targetIdx] = AgentState::Patrol;
                    entities.combatTarget[targetIdx] = UINT32_MAX;
                    entities.combatCooldown[targetIdx] = 2.0f;
                    
                    // Push agents apart to prevent immediate re-engagement
                    float dx = entities.posX[i] - entities.posX[targetIdx];
                    float dy = entities.posY[i] - entities.posY[targetIdx];
                    float dist = std::sqrt(dx * dx + dy * dy + 0.01f);
                    float separationDist = 25.0f;  // Push 25px apart
                    
                    entities.posX[i] += (dx / dist) * separationDist * 0.5f;
                    entities.posY[i] += (dy / dist) * separationDist * 0.5f;
                    entities.posX[targetIdx] -= (dx / dist) * separationDist * 0.5f;
                    entities.posY[targetIdx] -= (dy / dist) * separationDist * 0.5f;
                }
            }
        }
    }
    
    // Initiate new combats
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] != AgentType::Zombie) continue;
        if (entities.state[i] == AgentState::Fighting || entities.state[i] == AgentState::Dead) continue;
        if (entities.combatCooldown[i] > 0.0f) continue;  // Still on cooldown
        
        float px = entities.posX[i];
        float py = entities.posY[i];
        
        spatialHash.queryNeighbors(px, py, meleeRange, localNeighbors);
        
        for (uint32_t j : localNeighbors) {
            if (i == j) continue;
            
            AgentType otherType = entities.type[j];
            AgentState otherState = entities.state[j];
            
            // Skip if already fighting, dead, or bitten
            if (otherState == AgentState::Dead || otherState == AgentState::Fighting || otherState == AgentState::Bitten) continue;
            if (entities.combatCooldown[j] > 0.0f) continue;  // Target on cooldown
            if (otherType != AgentType::Civilian && otherType != AgentType::Hero) continue;
            
            float dx = entities.posX[i] - entities.posX[j];
            float dy = entities.posY[i] - entities.posY[j];
            float distSq = dx * dx + dy * dy;
            
            if (distSq < meleeRangeSq) {
                // Initiate combat!
                entities.state[i] = AgentState::Fighting;
                entities.state[j] = AgentState::Fighting;
                entities.combatTarget[i] = j;
                entities.combatTarget[j] = i;
                
                // Stop movement - agents are now locked in combat
                entities.velX[i] = 0.0f;
                entities.velY[i] = 0.0f;
                entities.velX[j] = 0.0f;
                entities.velY[j] = 0.0f;
                
                // Combat duration: 2-4 seconds (heroes fight faster)
                float duration = (otherType == AgentType::Hero) ? 
                    (1.0f + rng.range((uint32_t)i, RngUse::CombatDurationHero, 0, 10) / 10.0f) : 
                    (2.0f + rng.range((uint32_t)i, RngUse::CombatDurationCivilian, 0, 20) / 10.0f);
                    
                entities.combatTimer[i] = duration;
                entities.combatTimer[j] = duration;
                
                spdlog::info("Combat initiated: {} vs {} ({:.1f}s)", i, j, duration);
                break;  // One combat initiation per zombie per frame
            }
        }
    }
    
    // Zombie corpse feeding - regenerate health by consuming bodies
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.type[i] != AgentType::Zombie) continue;
        
        // Only feed if injured (health < 3)
        if (entities.health[i] >= 3) continue;
        
        float px = entities.posX[i];
        float py = entities.posY[i];
        
        spatialHash.queryNeighbors(px, py, feedRange, localNeighbors);
        
        for (uint32_t j : localNeighbors) {
            if (i == j) continue;
            
            // Look for corpses
            if (entities.state[j] != AgentState::Dead) continue;
            if (entities.type[j] != AgentType::Civilian) continue;  // Only feed on civilian corpses
            
            float dx = entities.posX[i] - entities.posX[j];
            float dy = entities.posY[i] - entities.posY[j];
            float distSq = dx * dx + dy * dy;
            
            if (distSq < feedRangeSq) {
                // Zombie feeds on corpse
                entities.health[i] = std::min(static_cast<uint8_t>(3), static_cast<uint8_t>(entities.health[i] + 1));
                corpsesToRemove.push_back(j);
                spdlog::info("Zombie {} fed on corpse {}, health now {}", i, j, entities.health[i]);
                break;  // One corpse per zombie per frame
            }
        }
    }
    
    // Remove consumed corpses
    std::sort(corpsesToRemove.begin(), corpsesToRemove.end(), std::greater<size_t>());
    corpsesToRemove.erase(std::unique(corpsesToRemove.begin(), corpsesToRemove.end()), corpsesToRemove.end());
    
    for (size_t idx : corpsesToRemove) {
        // Remove by swapping with last element and reducing count
        size_t lastIdx = entities.count - 1;
        if (idx != lastIdx) {
            entities.posX[idx] = entities.posX[lastIdx];
            entities.posY[idx] = entities.posY[lastIdx];
            entities.velX[idx] = entities.velX[lastIdx];
            entities.velY[idx] = entities.velY[lastIdx];
            entities.dirX[idx] = entities.dirX[lastIdx];
            entities.dirY[idx] = entities.dirY[lastIdx];
            entities.type[idx] = entities.type[lastIdx];
            entities.state[idx] = entities.state[lastIdx];
            entities.health[idx] = entities.health[lastIdx];
            entities.lastSeenX[idx] = entities.lastSeenX[lastIdx];
            entities.lastSeenY[idx] = entities.lastSeenY[lastIdx];
            entities.searchTimer[idx] = entities.searchTimer[lastIdx];
            entities.patrolTargetX[idx] = entities.patrolTargetX[lastIdx];
            entities.patrolTargetY[idx] = entities.patrolTargetY[lastIdx];
            entities.shootCooldown[idx] = entities.shootCooldown[lastIdx];
            entities.aimTimer[idx] = entities.aimTimer[lastIdx];
            entities.fleeStrategy[idx] = entities.fleeStrategy[lastIdx];
            entities.heroType[idx] = entities.heroType[lastIdx];
            entities.reanimationTimer[idx] = entities.reanimationTimer[lastIdx];
            entities.meleeAttackCooldown[idx] = entities.meleeAttackCooldown[lastIdx];
            entities.combatTarget[idx] = entities.combatTarget[lastIdx];
            entities.combatTimer[idx] = entities.combatTimer[lastIdx];
            entities.combatCooldown[idx] = entities.combatCooldown[lastIdx];
            entities.infectionTimer[idx] = entities.infectionTimer[lastIdx];
            entities.infectionProgress[idx] = entities.infectionProgress[lastIdx];
            prevPosX[idx] = prevPosX[lastIdx];
            prevPosY[idx] = prevPosY[lastIdx];
        }
        entities.count--;
        entities.posX.pop_back();
        entities.posY.pop_back();
        entities.velX.pop_back();
        entities.velY.pop_back();
        entities.dirX.pop_back();
        entities.dirY.pop_back();
        entities.type.pop_back();
        entities.state.pop_back();
        entities.health.pop_back();
        entities.lastSeenX.pop_back();
        entities.lastSeenY.pop_back();
        entities.searchTimer.pop_back();
        entities.patrolTargetX.pop_back();
        entities.patrolTargetY.pop_back();
        entities.shootCooldown.pop_back();
        entities.aimTimer.pop_back();
        entities.fleeStrategy.pop_back();
        entities.heroType.pop_back();
        entities.reanimationTimer.pop_back();
        entities.meleeAttackCooldown.pop_back();
        entities.combatTarget.pop_back();
        entities.combatTimer.pop_back();
        entities.combatCooldown.pop_back();
        entities.infectionTimer.pop_back();
        entities.infectionProgress.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
    }
    
    // Remove killed zombies (iterate backwards to maintain indices)
    std::sort(zombiesToKill.begin(), zombiesToKill.end(), std::greater<size_t>());
    for (size_t idx : zombiesToKill) {
        // Remove by swapping with last element and reducing count
        size_t lastIdx = entities.count - 1;
        if (idx != lastIdx) {
            entities.posX[idx] = entities.posX[lastIdx];
            entities.posY[idx] = entities.posY[lastIdx];
            entities.velX[idx] = entities.velX[lastIdx];
            entities.velY[idx] = entities.velY[lastIdx];
            entities.dirX[idx] = entities.dirX[lastIdx];
            entities.dirY[idx] = entities.dirY[lastIdx];
            entities.type[idx] = entities.type[lastIdx];
            entities.state[idx] = entities.state[lastIdx];
            entities.health[idx] = entities.health[lastIdx];
            entities.lastSeenX[idx] = entities.lastSeenX[lastIdx];
            entities.lastSeenY[idx] = entities.lastSeenY[lastIdx];
            entities.searchTimer[idx] = entities.searchTimer[lastIdx];
            entities.patrolTargetX[idx] = entities.patrolTargetX[lastIdx];
            entities.patrolTargetY[idx] = entities.patrolTargetY[lastIdx];
            entities.shootCooldown[idx] = entities.shootCooldown[lastIdx];
            entities.aimTimer[idx] = entities.aimTimer[lastIdx];
            entities.fleeStrategy[idx] = entities.fleeStrategy[lastIdx];
            entities.heroType[idx] = entities.heroType[lastIdx];
            entities.reanimationTimer[idx] = entities.reanimationTimer[lastIdx];
            entities.meleeAttackCooldown[idx] = entities.meleeAttackCooldown[lastIdx];
            entities.combatTarget[idx] = entities.combatTarget[lastIdx];
            entities.combatTimer[idx] = entities.combatTimer[lastIdx];
            entities.combatCooldown[idx] = entities.combatCooldown[lastIdx];
            entities.infectionTimer[idx] = entities.infectionTimer[lastIdx];
            entities.infectionProgress[idx] = entities.infectionProgress[lastIdx];
            prevPosX[idx] = prevPosX[lastIdx];
            prevPosY[idx] = prevPosY[lastIdx];
        }
        entities.count--;
        entities.posX.pop_back();
        entities.posY.pop_back();
        entities.velX.pop_back();
        entities.velY.pop_back();
        entities.dirX.pop_back();
        entities.dirY.pop_back();
        entities.type.pop_back();
        entities.state.pop_back();
        entities.health.pop_back();
        entities.lastSeenX.pop_back();
        entities.lastSeenY.pop_back();
        entities.searchTimer.pop_back();
        entities.patrolTargetX.pop_back();
        entities.patrolTargetY.pop_back();
        entities.shootCooldown.pop_back();
        entities.aimTimer.pop_back();
        entities.fleeStrategy.pop_back();
        entities.heroType.pop_back();
        entities.reanimationTimer.pop_back();
        entities.meleeAttackCooldown.pop_back();
        entities.combatTarget.pop_back();
        entities.combatTimer.pop_back();
        entities.combatCooldown.pop_back();
        entities.infectionTimer.pop_back();
        entities.infectionProgress.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
    }
}

void Simulation::resolveCivilianVsZombieCombat(size_t zombieIdx, size_t civilianIdx,
                                                int zombieAllies, int civilianAllies,
                                                std::vector<size_t>& zombiesToKill,
                                                std::vector<size_t>& entitiesToKill,
                                                const Rng& rng) {
    // Calculate outcome probabilities based on group sizes
    float survivalBonus = std::min(0.30f, civilianAllies * 0.15f);

    float killChance = 0.15f + survivalBonus;
    float killButBittenChance = 0.10f + (survivalBonus * 0.5f);
    float bittenEscapeChance = 0.30f;
    // "Civilian dies" is the effective remainder of the cascade below (the final `else`), not a
    // named branch keyed on its own probability. A `deathChance` value (and the `hordePenalty`
    // that fed it, from zombieAllies) used to be computed here but were never read; removed --
    // pure, side-effect-free computations, so removing them does not change behaviour. NOTE
    // (known, deliberately unfixed): killChance + killButBittenChance + bittenEscapeChance can
    // sum above 1.0 for high civilianAllies/low zombieAllies, which saturates the cascade so the
    // death `else` branch becomes unreachable. See branch review notes; not fixed in this wave
    // because it changes behaviour. `zombieAllies` is otherwise unused in this function now --
    // kept in the signature rather than restructuring the call sites.

    // Roll outcome. Keyed on the civilian: every write below targets it, and a
    // civilian resolves at most one combat per tick.
    int roll = rng.range((uint32_t)civilianIdx, RngUse::CivilianCombatRoll, 0, 99);
    float cumulative = 0.0f;
    
    if (roll < (cumulative += killChance * 100.0f)) {
        // Civilian kills zombie!
        zombiesToKill.push_back(zombieIdx);
        entities.state[civilianIdx] = AgentState::Fleeing;  // Run away
        spdlog::info("Civilian {} killed zombie {}!", civilianIdx, zombieIdx);
    }
    else if (roll < (cumulative += killButBittenChance * 100.0f)) {
        // Pyrrhic victory - kills zombie but gets bitten
        zombiesToKill.push_back(zombieIdx);
        entities.state[civilianIdx] = AgentState::Bitten;
        entities.infectionTimer[civilianIdx] = 5.0f + (rng.range((uint32_t)civilianIdx, RngUse::PyrrhicInfectionDuration, 0, 100) / 10.0f);  // 5-15 seconds
        entities.infectionProgress[civilianIdx] = 0.0f;
        spdlog::info("Civilian {} killed zombie {} but was bitten!", civilianIdx, zombieIdx);
    }
    else if (roll < (cumulative += bittenEscapeChance * 100.0f)) {
        // Bitten and escapes
        entities.state[civilianIdx] = AgentState::Bitten;
        entities.infectionTimer[civilianIdx] = 5.0f + (rng.range((uint32_t)civilianIdx, RngUse::BittenEscapeInfectionDuration, 0, 100) / 10.0f);
        entities.infectionProgress[civilianIdx] = 0.0f;
        spdlog::info("Civilian {} escaped but was bitten!", civilianIdx);
    }
    else {
        // Killed - becomes corpse
        entities.state[civilianIdx] = AgentState::Dead;
        entities.velX[civilianIdx] = 0.0f;
        entities.velY[civilianIdx] = 0.0f;
        entities.reanimationTimer[civilianIdx] = 3.0f + (rng.range((uint32_t)civilianIdx, RngUse::CombatDeathReanimationDelay, 0, 50) / 10.0f);
        spdlog::info("Civilian {} was killed by zombie {}!", civilianIdx, zombieIdx);
    }
}

void Simulation::resolveHeroVsZombieCombat(size_t heroIdx, size_t zombieIdx,
                                           std::vector<size_t>& zombiesToKill,
                                           std::vector<size_t>& entitiesToKill,
                                           const Rng& rng) {
    // Determine which is hero
    size_t actualHeroIdx = (entities.type[heroIdx] == AgentType::Hero) ? heroIdx : zombieIdx;
    size_t actualZombieIdx = (actualHeroIdx == heroIdx) ? zombieIdx : heroIdx;
    
    int roll = rng.range((uint32_t)actualHeroIdx, RngUse::HeroCombatRoll, 0, 99);
    
    if (roll < 80) {
        // Hero wins - kills zombie
        zombiesToKill.push_back(actualZombieIdx);
        entities.state[actualHeroIdx] = AgentState::Pursuing;  // Continue hunting
        spdlog::info("Hero {} killed zombie {}!", actualHeroIdx, actualZombieIdx);
    }
    else {
        // Hero takes damage
        if (entities.health[actualZombieIdx] > 0) {
            entities.health[actualZombieIdx]--;
            if (entities.health[actualZombieIdx] == 0) {
                zombiesToKill.push_back(actualZombieIdx);
            }
        }
        
        if (entities.health[actualHeroIdx] > 0) {
            entities.health[actualHeroIdx]--;
            if (entities.health[actualHeroIdx] == 0) {
                // Hero exhausted, becomes zombie
                entities.type[actualHeroIdx] = AgentType::Zombie;
                entities.health[actualHeroIdx] = 3;
                spdlog::info("Hero {} exhausted and turned zombie!", actualHeroIdx);
            }
        }
        spdlog::info("Hero {} vs Zombie {} - both damaged!", actualHeroIdx, actualZombieIdx);
    }
}
