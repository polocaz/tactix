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

    // Spawn civilians
    for (size_t i = 0; i < civilianCount; i++) {
        float px = (float)GetRandomValue(0, screenWidth);
        float py = (float)GetRandomValue(0, screenHeight);
        float vx = (float)GetRandomValue(-20, 20);
        float vy = (float)GetRandomValue(-20, 20);
        entities.spawn(px, py, vx, vy, AgentType::Civilian);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn zombies
    for (size_t i = 0; i < zombieCount; i++) {
        float px = (float)GetRandomValue(0, screenWidth);
        float py = (float)GetRandomValue(0, screenHeight);
        float vx = (float)GetRandomValue(-15, 15);
        float vy = (float)GetRandomValue(-15, 15);
        entities.spawn(px, py, vx, vy, AgentType::Zombie);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Spawn heroes
    for (size_t i = 0; i < heroCount; i++) {
        float px = (float)GetRandomValue(0, screenWidth);
        float py = (float)GetRandomValue(0, screenHeight);
        float vx = (float)GetRandomValue(-25, 25);
        float vy = (float)GetRandomValue(-25, 25);
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
    // Store previous positions for interpolation
    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
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
        entities.posX[i] += entities.velX[i] * dt;
        entities.posY[i] += entities.velY[i] * dt;
        
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
        float targetSpeed = 60.0f;  // Base civilian speed
        if (myType == AgentType::Zombie) targetSpeed = 55.0f;    // Zombies slightly slower but relentless
        else if (myType == AgentType::Hero) targetSpeed = 75.0f;  // Heroes fastest
        
        // Different behaviors based on agent type and state
        if (myType == AgentType::Civilian) {
            // Check for nearby zombies
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
                }
            }
            
            if (targetFound) {
                // Flee at panic speed
                entities.state[i] = AgentState::Fleeing;
                targetSpeed = 65.0f;  // Panic boost
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
                            targetSpeed = 65.0f;  // Sprint!
                        }
                    }
                }
            }
            
            if (targetFound) {
                entities.state[i] = AgentState::Pursuing;
            } else if (myState == AgentState::Pursuing) {
                entities.state[i] = AgentState::Searching;
                entities.searchTimer[i] = searchDuration * 2.0f;
            }
            
            if (myState == AgentState::Searching) {
                entities.searchTimer[i] -= dt;
                desiredDirX = entities.lastSeenX[i] - px;
                desiredDirY = entities.lastSeenY[i] - py;
                float dist = std::sqrt(desiredDirX * desiredDirX + desiredDirY * desiredDirY + 0.01f);
                targetCount = 1;
                targetSpeed = 45.0f;
                
                if (dist < 5.0f || entities.searchTimer[i] <= 0) {
                    entities.state[i] = AgentState::Patrol;
                }
            }
            
        } else if (myType == AgentType::Hero) {
            // Seek zombies aggressively
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
                        
                        // Update memory
                        entities.lastSeenX[i] = entities.posX[neighborIdx];
                        entities.lastSeenY[i] = entities.posY[neighborIdx];
                    }
                }
            }
            
            if (targetFound) {
                entities.state[i] = AgentState::Pursuing;
                targetSpeed = 80.0f;  // Sprint toward zombies
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
                        entities.health[j] = 0;
                        // Reverse velocity to show disorientation
                        entities.velX[j] = -entities.velX[j] * 0.5f;
                        entities.velY[j] = -entities.velY[j] * 0.5f;
                    } else if (otherType == AgentType::Hero) {
                        // Hero kills zombie
                        if (entities.health[j] > 0) {
                            entities.health[j]--;
                            zombiesToKill.push_back(i);  // Mark zombie for removal
                            
                            if (entities.health[j] == 0) {
                                // Hero becomes zombie after 5 kills (exhaustion)
                                entities.type[j] = AgentType::Zombie;
                                spdlog::info("Hero {} exhausted after 5 kills, turned zombie!", j);
                            }
                            break;  // Zombie is dead, stop checking
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
}
