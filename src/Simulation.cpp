#include "platform.h"
#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>
#include <chrono>
#include "spdlog/spdlog.h"

Simulation::Simulation(int w, int h)
    : screenWidth(w), screenHeight(h)
    , spatialHash(static_cast<float>(w), static_cast<float>(h), 50.0f)  // 50 pixel cells (Design Doc §5.1)
{
    neighborBuffer.reserve(200);  // Pre-allocate for typical neighbor count
}

void Simulation::init(size_t count) {
    spdlog::info("Initializing {} agents with zombie simulation", count);
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
            px = building.x + building.width / 2.0f + (float)GetRandomValue(-60, 60);
            py = building.y + building.height / 2.0f + (float)GetRandomValue(-60, 60);
        } else {
            px = (float)GetRandomValue(0, screenWidth);
            py = (float)GetRandomValue(0, screenHeight);
        }
        float vx = (float)GetRandomValue(-10, 10);
        float vy = (float)GetRandomValue(-10, 10);
        entities.spawn(px, py, vx, vy, AgentType::Civilian);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn zombies at graveyard (bottom-left area)
    for (size_t i = 0; i < zombieCount; i++) {
        float px = (float)GetRandomValue(50, 250);  // Graveyard zone
        float py = (float)GetRandomValue(screenHeight - 250, screenHeight - 50);
        float vx = (float)GetRandomValue(-8, 8);
        float vy = (float)GetRandomValue(-8, 8);
        entities.spawn(px, py, vx, vy, AgentType::Zombie);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn heroes spread out (strategic positions)
    for (size_t i = 0; i < heroCount; i++) {
        // Spread heroes around perimeter
        float px = (float)GetRandomValue(screenWidth / 3, screenWidth * 2 / 3);
        float py = (float)GetRandomValue(50, 200);  // Top area
        float vx = (float)GetRandomValue(-12, 12);
        float vy = (float)GetRandomValue(-12, 12);
        entities.spawn(px, py, vx, vy, AgentType::Hero);
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
    // City blocks (buildings)
    const int blockCount = 8;
    for (int i = 0; i < blockCount; i++) {
        float x = (float)GetRandomValue(100, screenWidth - 200);
        float y = (float)GetRandomValue(100, screenHeight - 200);
        float w = (float)GetRandomValue(80, 150);
        float h = (float)GetRandomValue(80, 150);
        buildings.push_back({x, y, w, h});
    }
    
    // Scattered trees
    const int treeCount = 30;
    for (int i = 0; i < treeCount; i++) {
        float x = (float)GetRandomValue(50, screenWidth - 50);
        float y = (float)GetRandomValue(50, screenHeight - 50);
        float r = (float)GetRandomValue(15, 25);
        trees.push_back({x, y, r});
    }
    
    spdlog::info("Generated {} buildings and {} trees", buildings.size(), trees.size());
}

void Simulation::setAgentCount(size_t count) {
    if (count == entities.count) return;
    
    if (count > entities.count) {
        // Add more agents with proper distribution
        size_t toAdd = count - entities.count;
        size_t civiliansToAdd = static_cast<size_t>(toAdd * 0.90f);
        size_t zombiesToAdd = static_cast<size_t>(toAdd * 0.05f);
        size_t heroesToAdd = toAdd - civiliansToAdd - zombiesToAdd;
        
        for (size_t i = 0; i < civiliansToAdd; i++) {
            float px = (float)GetRandomValue(0, screenWidth);
            float py = (float)GetRandomValue(0, screenHeight);
            float vx = (float)GetRandomValue(-20, 20);
            float vy = (float)GetRandomValue(-20, 20);
            entities.spawn(px, py, vx, vy, AgentType::Civilian);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        
        for (size_t i = 0; i < zombiesToAdd; i++) {
            float px = (float)GetRandomValue(0, screenWidth);
            float py = (float)GetRandomValue(0, screenHeight);
            float vx = (float)GetRandomValue(-15, 15);
            float vy = (float)GetRandomValue(-15, 15);
            entities.spawn(px, py, vx, vy, AgentType::Zombie);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        
        for (size_t i = 0; i < heroesToAdd; i++) {
            float px = (float)GetRandomValue(0, screenWidth);
            float py = (float)GetRandomValue(0, screenHeight);
            float vx = (float)GetRandomValue(-25, 25);
            float vy = (float)GetRandomValue(-25, 25);
            entities.spawn(px, py, vx, vy, AgentType::Hero);
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
    updateSeparation(dt);  // Collision avoidance using spatial queries
    updateBehaviors(dt);   // Seek/flee/combat behaviors for zombie simulation
    updateMovement(dt);    // Apply velocities
    
    // Process infections (main thread, requires state changes)
    updateInfections();
    
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

void Simulation::updateSeparation(float dt) {
    // Parallelize collision avoidance (Design Doc §6.2)
    const size_t chunkSize = 256;  // Job granularity
    
    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        jobSystem.submit([this, start, end, dt]() {
            updateSeparationChunk(start, end, dt);
        });
    }
    
    jobSystem.waitAll();  // Barrier (Design Doc §6.3)
}

void Simulation::updateSeparationChunk(size_t start, size_t end, float dt) {
    // Collision avoidance using spatial queries (Phase 2)
    const float separationRadius = 20.0f;
    const float separationStrength = 200.0f;
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
        for (const auto& building : buildings) {
            // Find closest point on rectangle to agent
            float closestX = std::max(building.x, std::min(px, building.x + building.width));
            float closestY = std::max(building.y, std::min(py, building.y + building.height));
            
            float dx = px - closestX;
            float dy = py - closestY;
            float distSq = dx * dx + dy * dy;
            
            const float obstacleAvoidDist = 50.0f;  // Start avoiding earlier
            if (distSq < obstacleAvoidDist * obstacleAvoidDist) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly in any direction
                    steerX += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (obstacleAvoidDist - dist) / obstacleAvoidDist;
                    steerX += (dx / dist) * force * 5.0f;  // Much stronger avoidance
                    steerY += (dy / dist) * force * 5.0f;
                }
            }
        }
        
        // Obstacle avoidance - trees (circles)
        for (const auto& tree : trees) {
            float dx = px - tree.x;
            float dy = py - tree.y;
            float distSq = dx * dx + dy * dy;
            float avoidRadius = tree.radius + 20.0f;  // Extra buffer
            
            if (distSq < avoidRadius * avoidRadius) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly
                    steerX += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
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
    
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.posX[i] < 0) entities.posX[i] += w;
        if (entities.posX[i] > w) entities.posX[i] -= w;
        if (entities.posY[i] < 0) entities.posY[i] += h;
        if (entities.posY[i] > h) entities.posY[i] -= h;
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

void Simulation::updateBehaviors(float dt) {
    // Parallelize behavior updates
    const size_t chunkSize = 256;
    
    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        jobSystem.submit([this, start, end, dt]() {
            updateBehaviorsChunk(start, end, dt);
        });
    }
    
    jobSystem.waitAll();
}

void Simulation::updateBehaviorsChunk(size_t start, size_t end, float dt) {
    const float seekRadius = 150.0f;  // Detection range
    const float searchDuration = 3.0f;  // Seconds to search last known location
    const float wanderStrength = 20.0f;
    
    // Thread-local neighbor buffer
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);
    
    for (size_t i = start; i < end; i++) {
        AgentType myType = entities.type[i];
        AgentState myState = entities.state[i];
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
                    entities.fleeStrategy[i] = (GetRandomValue(0, 100) < 30) ? 1 : 0;
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
                    entities.aimTimer[i] = 0.3f + ((float)GetRandomValue(0, 300) / 1000.0f);
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
                entities.patrolTargetX[i] = (float)GetRandomValue(50, 1850);
                entities.patrolTargetY[i] = (float)GetRandomValue(50, 1030);
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
        
        // Clamp to max speed
        float speedSq = entities.velX[i] * entities.velX[i] + entities.velY[i] * entities.velY[i];
        float maxSpeed = targetSpeed * 1.1f;
        if (speedSq > maxSpeed * maxSpeed) {
            float speed = std::sqrt(speedSq);
            entities.velX[i] = (entities.velX[i] / speed) * maxSpeed;
            entities.velY[i] = (entities.velY[i] / speed) * maxSpeed;
        }
    }
}

void Simulation::updateInfections() {
    const float infectionRadius = 15.0f;  // Close contact required
    const float infectionRadiusSq = infectionRadius * infectionRadius;
    
    std::vector<size_t> zombiesToKill;  // Track zombies to remove
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);
    
    // Use spatial hash to avoid O(n²) - much more efficient!
    for (size_t i = 0; i < entities.count; i++) {
        AgentType myType = entities.type[i];
        
        if (myType == AgentType::Zombie) {
            float px = entities.posX[i];
            float py = entities.posY[i];
            
            // Query only nearby agents (O(k) instead of O(n))
            spatialHash.queryNeighbors(px, py, infectionRadius, localNeighbors);
            
            for (uint32_t j : localNeighbors) {
                if (i == j) continue;
                
                AgentType otherType = entities.type[j];
                if (otherType != AgentType::Civilian && otherType != AgentType::Hero) continue;
                
                float dx = entities.posX[i] - entities.posX[j];
                float dy = entities.posY[i] - entities.posY[j];
                float distSq = dx * dx + dy * dy;
                
                if (distSq < infectionRadiusSq) {
                    if (otherType == AgentType::Civilian) {
                        // Civilian becomes zombie
                        entities.type[j] = AgentType::Zombie;
                        entities.health[j] = 3;  // New zombie has 3 health
                        // Reverse velocity to show disorientation
                        entities.velX[j] = -entities.velX[j] * 0.5f;
                        entities.velY[j] = -entities.velY[j] * 0.5f;
                    } else if (otherType == AgentType::Hero) {
                        // Hero damages zombie in melee
                        if (entities.health[i] > 0) {
                            entities.health[i]--;  // Damage zombie
                            if (entities.health[i] == 0) {
                                zombiesToKill.push_back(i);  // Zombie dies
                            }
                            
                            // Hero loses health (track kills)
                            if (entities.health[j] > 0) {
                                entities.health[j]--;
                                if (entities.health[j] == 0) {
                                    // Hero becomes zombie after 5 kills (exhaustion)
                                    entities.type[j] = AgentType::Zombie;
                                    entities.health[j] = 3;  // New zombie has 3 health
                                    spdlog::info("Hero {} exhausted after 5 kills, turned zombie!", j);
                                }
                            }
                            break;  // Zombie is damaged, stop checking
                        }
                    }
                }
            }
        }
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
            entities.health[idx] = entities.health[lastIdx];
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
        entities.health.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
    }
}

void Simulation::draw(float alpha) {
    // Draw simulation world boundary
    const float borderThickness = 3.0f;
    DrawRectangleLinesEx(
        Rectangle{0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight)},
        borderThickness,
        Color{100, 150, 255, 255}
    );
    
    // Draw graveyard
    DrawRectangle(
        static_cast<int>(graveyard.x),
        static_cast<int>(graveyard.y),
        static_cast<int>(graveyard.width),
        static_cast<int>(graveyard.height),
        Color{40, 35, 45, 255}  // Dark purple-gray
    );
    // Tombstones
    for (int i = 0; i < 8; i++) {
        float tx = graveyard.x + 30 + (i % 3) * 60;
        float ty = graveyard.y + 40 + (i / 3) * 60;
        DrawRectangle(static_cast<int>(tx), static_cast<int>(ty), 20, 30, Color{80, 75, 85, 255});
        DrawRectangle(static_cast<int>(tx + 5), static_cast<int>(ty - 5), 10, 10, Color{90, 85, 95, 255});
    }
    DrawText("GRAVEYARD", static_cast<int>(graveyard.x + 50), static_cast<int>(graveyard.y + 10), 16, Color{120, 110, 130, 255});
    
    // Debug: Draw grid
    if (debugGrid) {
        const float cellSize = 50.0f;
        for (int x = 0; x < screenWidth; x += static_cast<int>(cellSize)) {
            DrawLine(x, 0, x, screenHeight, Color{80, 255, 100, 180});
        }
        for (int y = 0; y < screenHeight; y += static_cast<int>(cellSize)) {
            DrawLine(0, y, screenWidth, y, Color{80, 255, 100, 180});
        }
    }
    
    // Interpolated rendering with directional triangles
    // Triangles show movement direction - useful for AI visualization
    const float agentSize = 4.0f;
    const float wrapThreshold = static_cast<float>(screenWidth) * 0.5f;  // Detect wrapping
    
    for (size_t i = 0; i < entities.count; i++) {
        // Check if agent wrapped this frame (large position delta)
        float deltaX = std::abs(entities.posX[i] - prevPosX[i]);
        float deltaY = std::abs(entities.posY[i] - prevPosY[i]);
        
        // If wrapped, don't interpolate (use current position to avoid stretching)
        float renderX, renderY;
        if (deltaX > wrapThreshold || deltaY > wrapThreshold) {
            renderX = entities.posX[i];
            renderY = entities.posY[i];
        } else {
            renderX = prevPosX[i] + (entities.posX[i] - prevPosX[i]) * alpha;
            renderY = prevPosY[i] + (entities.posY[i] - prevPosY[i]) * alpha;
        }
        
        // Calculate triangle vertices pointing in direction of movement
        float dx = entities.dirX[i];
        float dy = entities.dirY[i];
        
        // Front vertex (pointing forward)
        float frontX = renderX + dx * agentSize;
        float frontY = renderY + dy * agentSize;
        
        // Perpendicular for base vertices
        float perpX = -dy;
        float perpY = dx;
        
        // Base vertices
        float baseLeft_X = renderX - perpX * (agentSize * 0.4f);
        float baseLeft_Y = renderY - perpY * (agentSize * 0.4f);
        float baseRight_X = renderX + perpX * (agentSize * 0.4f);
        float baseRight_Y = renderY + perpY * (agentSize * 0.4f);
        
        // Color based on agent type
        Color agentColor;
        if (entities.type[i] == AgentType::Civilian) {
            agentColor = Color{220, 220, 220, 255};  // Light gray/white
        } else if (entities.type[i] == AgentType::Zombie) {
            agentColor = Color{50, 200, 50, 255};     // Green
        } else {  // Hero
            // Color heroes based on health (blue gradient)
            uint8_t health = entities.health[i];
            uint8_t brightness = 100 + (health * 30);  // Brighter with more health
            agentColor = Color{50, 100, brightness, 255};
        }
        
        DrawTriangle(
            Vector2{frontX, frontY},
            Vector2{baseLeft_X, baseLeft_Y},
            Vector2{baseRight_X, baseRight_Y},
            agentColor
        );
    }
    
    // Draw gunshot lines (visualize shooting)
    for (const auto& line : gunshotLines) {
        // Fade based on lifetime (0.15s total)
        float alpha_val = line.lifetime / 0.15f;
        uint8_t alpha_byte = static_cast<uint8_t>(alpha_val * 255.0f);
        DrawLineEx(
            Vector2{line.fromX, line.fromY},
            Vector2{line.toX, line.toY},
            0.8f,  // Thin line
            Color{255, 255, 0, alpha_byte}  // Bright yellow, fading
        );
    }
    
    // Draw buildings
    for (const auto& building : buildings) {
        DrawRectangle(
            static_cast<int>(building.x),
            static_cast<int>(building.y),
            static_cast<int>(building.width),
            static_cast<int>(building.height),
            Color{80, 80, 90, 255}  // Dark gray
        );
        // Outline
        DrawRectangleLines(
            static_cast<int>(building.x),
            static_cast<int>(building.y),
            static_cast<int>(building.width),
            static_cast<int>(building.height),
            Color{60, 60, 70, 255}
        );
    }
    
    // Draw trees
    for (const auto& tree : trees) {
        DrawCircle(
            static_cast<int>(tree.x),
            static_cast<int>(tree.y),
            tree.radius,
            Color{40, 120, 40, 255}  // Forest green
        );
        // Darker center for depth
        DrawCircle(
            static_cast<int>(tree.x),
            static_cast<int>(tree.y),
            tree.radius * 0.6f,
            Color{30, 90, 30, 255}
        );
    }
}
